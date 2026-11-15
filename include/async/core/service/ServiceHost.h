#pragma once
#include <async/config/macros.h>
#include <async/core/service/AnyServiceRef.h>
#include <async/core/event/declarations/concepts.h>
#include <async/core/details/containers/FixedCapacityVector.h>
#include <async/core/error.h>
#include <limits>
#include <mutex>
#include <optional>
LCF_ASYNC_EXPORT
namespace lcf::async {
template <event_backend_c Backend>
class ServiceHost
{
    using Clock = ServiceHandle::clock_t;
    static constexpr std::size_t k_no_index = std::numeric_limits<std::size_t>::max();
    struct Entry
    {
        ~Entry() noexcept = default;
        Entry(ServiceHost & host, AnyServiceRef service, std::size_t index) noexcept :
            m_host_p(&host), m_service(service), m_index(index) {}
        Entry(const Entry &) = delete;
        Entry(Entry &&) = delete;
        Entry & operator=(const Entry &) = delete;
        Entry & operator=(Entry &&) = delete;
        ServiceHost * m_host_p;
        AnyServiceRef m_service;
        std::size_t m_index;
        std::size_t m_heap_index = k_no_index;
        Clock::time_point m_wake_time = Clock::time_point::max();
        bool m_active = false;
        bool m_ready = false;
        bool m_stop_requested = false;
    };
    static void notify(void * p, ServiceHandle::Notification notification, Clock::time_point time) noexcept
    {
        Entry & entry = *static_cast<Entry *>(p);
        entry.m_host_p->notify(entry, notification, time);
    }
public:
    ~ServiceHost() noexcept = default;
    ServiceHost() = delete;
    explicit ServiceHost(Backend & backend) noexcept : m_backend_p(&backend) {}
    ServiceHost(const ServiceHost &) = delete;
    ServiceHost(ServiceHost &&) = delete;
    ServiceHost & operator=(const ServiceHost &) = delete;
    ServiceHost & operator=(ServiceHost &&) = delete;
    template <typename Layout> static void appendStorage(std::size_t capacity, Layout & layout)
    {
        layout.template append<Entry>(capacity);
        layout.template append<std::size_t>(capacity);
        layout.template append<std::size_t>(capacity);
    }
    template <typename Storage> void prepareStorage(std::size_t capacity, Storage & storage)
    {
        storage.reserve(m_entries, capacity);
        storage.reserve(m_ready, capacity);
        storage.reserve(m_timers, capacity);
    }
    std::error_code registerService(AnyServiceRef service) noexcept
    {
        const std::lock_guard lock {m_mutex};
        if (m_registration_closed) { return errc::service_registration_closed; }
        if (m_entries.size() == m_entries.capacity()) { return std::make_error_code(std::errc::no_buffer_space); }
        m_entries.emplace_back(*this, service, m_entries.size());
        m_ready.emplace_back(0);
        return {};
    }
    void closeRegistration() noexcept { const std::lock_guard lock {m_mutex}; m_registration_closed = true; }
    std::error_code enable() noexcept
    {
        { const std::lock_guard lock {m_mutex}; m_registration_closed = true; if (m_enabled) { return {}; } m_enabled = true; }
        for (Entry & entry : m_entries) {
            { const std::lock_guard lock {m_mutex}; entry.m_active = true; ++m_active; }
            const std::error_code ec = entry.m_service.start(ServiceHandle {&entry, &notify});
            if (ec) { return ec; }
        }
        return {};
    }
    void requestStop() noexcept
    {
        this->closeRegistration();
        for (Entry & entry : m_entries) {
            bool request;
            { const std::lock_guard lock {m_mutex}; request = entry.m_active and not entry.m_stop_requested; entry.m_stop_requested = true; }
            if (request) { entry.m_service.requestStop(); }
        }
    }
    bool isStopped() const noexcept { const std::lock_guard lock {m_mutex}; return m_active == 0; }
    Clock::time_point getWakeTime() const noexcept
    {
        const std::lock_guard lock {m_mutex};
        if (m_ready_count) { return Clock::time_point::min(); }
        return m_timers.empty() ? Clock::time_point::max() : m_entries[m_timers.front()].m_wake_time;
    }
    DriveReport driveOne() noexcept
    {
        Entry * entry_p = nullptr;
        {
            const std::lock_guard lock {m_mutex};
            const Clock::time_point now = Clock::now();
            if (not m_timers.empty() and m_entries[m_timers.front()].m_wake_time <= now) {
                Entry & entry = m_entries[m_timers.front()];
                this->removeTimer(entry);
                this->queueReady(entry);
            }
            if (m_ready_count) {
                Entry & entry = m_entries[m_ready[m_ready_head]];
                m_ready_head = (m_ready_head + 1) % m_ready.size();
                --m_ready_count;
                entry.m_ready = false;
                if (entry.m_active) { entry_p = &entry; }
            }
        }
        return entry_p ? entry_p->m_service.driveOne() : DriveReport {};
    }
private:
    void queueReady(Entry & entry) noexcept
    {
        if (not entry.m_active or entry.m_ready) { return; }
        m_ready[(m_ready_head + m_ready_count) % m_ready.size()] = entry.m_index;
        ++m_ready_count;
        entry.m_ready = true;
    }
    void swapTimer(std::size_t left, std::size_t right) noexcept
    {
        std::swap(m_timers[left], m_timers[right]);
        m_entries[m_timers[left]].m_heap_index = left;
        m_entries[m_timers[right]].m_heap_index = right;
    }
    bool earlier(std::size_t left, std::size_t right) const noexcept
    { return m_entries[m_timers[left]].m_wake_time < m_entries[m_timers[right]].m_wake_time; }
    void repairTimer(std::size_t index) noexcept
    {
        while (index and this->earlier(index, (index - 1) / 2)) { const auto parent = (index - 1) / 2; this->swapTimer(index, parent); index = parent; }
        while (2 * index + 1 < m_timers.size()) {
            std::size_t child = 2 * index + 1;
            if (child + 1 < m_timers.size() and this->earlier(child + 1, child)) { ++child; }
            if (not this->earlier(child, index)) { break; }
            this->swapTimer(index, child); index = child;
        }
    }
    void removeTimer(Entry & entry) noexcept
    {
        if (entry.m_heap_index == k_no_index) { return; }
        const auto index = entry.m_heap_index;
        this->swapTimer(index, m_timers.size() - 1);
        m_timers.pop_back();
        entry.m_heap_index = k_no_index;
        if (index < m_timers.size()) { this->repairTimer(index); }
    }
    void notify(Entry & entry, ServiceHandle::Notification notification, Clock::time_point time) noexcept
    {
        {
            const std::lock_guard lock {m_mutex};
            if (not entry.m_active) { return; }
            if (notification == ServiceHandle::Notification::eReady) { this->queueReady(entry); }
            else if (notification == ServiceHandle::Notification::eStopped) {
                entry.m_active = false; --m_active; this->removeTimer(entry);
            } else {
                this->removeTimer(entry);
                entry.m_wake_time = time;
                if (time != Clock::time_point::max()) {
                    entry.m_heap_index = m_timers.size(); m_timers.emplace_back(entry.m_index); this->repairTimer(entry.m_heap_index);
                }
            }
        }
        m_backend_p->wake();
    }
    Backend * m_backend_p;
    details::FixedCapacityVector<Entry> m_entries;
    details::FixedCapacityVector<std::size_t> m_ready;
    details::FixedCapacityVector<std::size_t> m_timers;
    mutable std::mutex m_mutex;
    std::size_t m_ready_head = 0, m_ready_count = 0, m_active = 0;
    bool m_registration_closed = false, m_enabled = false;
};
}
