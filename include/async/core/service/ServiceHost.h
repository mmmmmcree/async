#pragma once

#include <async/config/macros.h>
#include <async/core/service/AnyServiceRef.h>
#include <async/core/service/declarations/enums.h>
#include <async/core/notify/declarations/concepts.h>
#include <async/core/details/containers/FixedCapacityList.h>
#include <async/core/details/containers/IndexedHeap.h>
#include <async/core/details/StorageLayout.h>
#include <async/core/queue/intrusive/Queue.h>
#include <async/core/error.h>

#include <concepts>
#include <cstdint>
#include <limits>
#include <mutex>
#include <new>
#include <variant>

LCF_ASYNC_EXPORT
namespace lcf::async {

template <notify::event_pump_c EventPump>
class ServiceHost
{
    using Clock = ServiceHandle::clock_t;
    using Notification = ServiceHandle::notification_t;
    enum LifecycleFlags : std::uint8_t { eNone = 0, eCreated = 1, eEnabled = 2, eRegistrationClosed = 4 };
    static constexpr std::size_t k_no_index = std::numeric_limits<std::size_t>::max();
    struct Entry
    {
        ~Entry() noexcept = default;
        Entry() = delete;
        Entry(ServiceHost & host, AnyServiceRef service) noexcept : m_host_p(&host), m_service(service) {}
        Entry(const Entry &) = delete;
        Entry(Entry &&) = delete;
        Entry & operator=(const Entry &) = delete;
        Entry & operator=(Entry &&) = delete;
    public:
        Entry * getNext() const noexcept { return m_next_p; }
        void setNext(Entry * next_p) noexcept { m_next_p = next_p; }
    public:
        ServiceHost * m_host_p;
        AnyServiceRef m_service;
        Entry * m_next_p = nullptr;
        std::size_t m_heap_index = k_no_index;
        Clock::time_point m_wake_time = Clock::time_point::max();
        bool m_active = false;
        bool m_stop_requested = false;
    };
    struct WakeEarlier
    {
        bool operator()(const Entry & left, const Entry & right) const noexcept { return left.m_wake_time < right.m_wake_time; }
    };
    struct HeapPosition
    {
        std::size_t & operator()(Entry & entry) const noexcept { return entry.m_heap_index; }
    };
    using Entries = details::FixedCapacityList<Entry>;
    using Timers = details::IndexedHeap<Entry, WakeEarlier, HeapPosition>;
    using ReadyQueue = queue::intrusive::Queue<Entry>;
    template <typename Storage>
    static void reserve_storage(std::size_t capacity, Storage & storage, Entries & entries, Timers & timers)
    {
        storage.reserve(entries, capacity);
        storage.reserve(timers, capacity);
    }
public:
    static std::expected<StorageRequirements, std::error_code> get_storage_requirements(std::size_t capacity) noexcept
    {
        Entries entries;
        Timers timers;
        details::StorageLayout layout;
        reserve_storage(capacity, layout, entries, timers);
        return layout.getSize();
    }
public:
    ~ServiceHost() noexcept = default;
    ServiceHost() = delete;
    explicit ServiceHost(EventPump & event_pump) noexcept : m_event_pump_p(&event_pump) {}
    ServiceHost(const ServiceHost &) = delete;
    ServiceHost(ServiceHost &&) = delete;
    ServiceHost & operator=(const ServiceHost &) = delete;
    ServiceHost & operator=(ServiceHost &&) = delete;
public:
    std::error_code create(std::size_t capacity, std::pmr::memory_resource & resource) noexcept
    {
        const std::lock_guard lock {m_mutex};
        if (m_lifecycle_flags & LifecycleFlags::eCreated) { return std::make_error_code(std::errc::operation_in_progress); }
        if (const auto requirements = get_storage_requirements(capacity); not requirements) { return requirements.error(); }
        try {
            Entries entries;
            Timers timers;
            details::StorageAllocator storage {resource};
            reserve_storage(capacity, storage, entries, timers);
            m_entries = std::move(entries);
            m_timers = std::move(timers);
        } catch (const std::bad_alloc &) { return std::make_error_code(std::errc::not_enough_memory); }
        catch (const std::system_error & exception) { return exception.code(); }
        catch (...) { return errc::unexpected_exception; }
        m_lifecycle_flags |= LifecycleFlags::eCreated;
        return {};
    }
    std::error_code registerService(AnyServiceRef service) noexcept
    {
        const std::lock_guard lock {m_mutex};
        if (not (m_lifecycle_flags & LifecycleFlags::eCreated)) { return std::make_error_code(std::errc::operation_not_permitted); }
        if (m_lifecycle_flags & LifecycleFlags::eRegistrationClosed) { return errc::service_registration_closed; }
        if (m_entries.size() == m_entries.capacity()) { return std::make_error_code(std::errc::no_buffer_space); }
        m_entries.emplace_back(*this, service);
        return {};
    }
    void closeRegistration() noexcept { const std::lock_guard lock {m_mutex}; m_lifecycle_flags |= LifecycleFlags::eRegistrationClosed; }
    std::error_code enable() noexcept
    {
        {
            const std::lock_guard lock {m_mutex};
            if (not (m_lifecycle_flags & LifecycleFlags::eCreated)) { return std::make_error_code(std::errc::operation_not_permitted); }
            if (m_lifecycle_flags & LifecycleFlags::eEnabled) { return {}; }
            m_lifecycle_flags |= LifecycleFlags::eEnabled | LifecycleFlags::eRegistrationClosed;
        }
        for (Entry & entry : m_entries) {
            {
                const std::lock_guard lock {m_mutex};
                entry.m_active = true;
                ++m_active;
            }
            const auto ec = entry.m_service.start(ServiceHandle {&entry, &notify});
            if (ec) { return ec; }
        }
        return {};
    }
    void requestStop() noexcept
    {
        this->closeRegistration();
        for (Entry & entry : m_entries) {
            bool request;
            {
                const std::lock_guard lock {m_mutex};
                request = entry.m_active and not entry.m_stop_requested;
                entry.m_stop_requested = true;
            }
            if (request) { entry.m_service.requestStop(); }
        }
    }
    bool isStopped() const noexcept { const std::lock_guard lock {m_mutex}; return m_active == 0; }
    Clock::time_point getWakeTime() const noexcept
    {
        const std::lock_guard lock {m_mutex};
        if (not m_ready_services.isEmpty()) { return Clock::time_point::min(); }
        const Entry * entry_p = m_timers.top();
        return entry_p ? entry_p->m_wake_time : Clock::time_point::max();
    }
    std::error_code driveOne() noexcept
    {
        Entry * entry_p = nullptr;
        std::error_code ec;
        {
            const std::lock_guard lock {m_mutex};
            ec = m_error_code;
            if (const Entry * timer_p = m_timers.top(); timer_p and timer_p->m_wake_time <= Clock::now()) {
                this->queueReady(*m_timers.pop());
            }
            entry_p = m_ready_services.pop();
            if (entry_p and not entry_p->m_active) { entry_p = nullptr; }
        }
        const auto service_ec = entry_p ? entry_p->m_service.driveOne() : make_error_code(errc::no_work);
        return ec ? ec : service_ec;
    }
private:
    void queueReady(Entry & entry) noexcept
    {
        if (entry.m_active and not entry.getNext()) { m_ready_services.pushBack(entry); }
    }
    static void notify(void * entry_p, Notification notification) noexcept
    {
        Entry & entry = *static_cast<Entry *>(entry_p);
        ServiceHost & host = *entry.m_host_p;
        const bool accepted = std::visit([&host, &entry]<typename Request>(Request request) noexcept {
            constexpr bool is_signal = std::same_as<Request, ServiceSignal>;
            auto deadline = Clock::time_point::min();
            if constexpr (std::same_as<Request, Clock::duration>) {
                if (request > Clock::duration::zero()) {
                    constexpr auto latest_deadline = Clock::time_point::max() - Clock::duration {1};
                    const auto now = Clock::now();
                    deadline = now > latest_deadline - request ? latest_deadline : now + request;
                }
            } else if constexpr (not is_signal) { deadline = request; }
            const std::lock_guard lock {host.m_mutex};
            if (not entry.m_active) { return false; }
            if constexpr (is_signal) {
                if (request == ServiceSignal::eReady) { host.queueReady(entry); }
                else {
                    entry.m_active = false;
                    --host.m_active;
                    host.m_timers.erase(entry);
                }
            } else {
                entry.m_wake_time = deadline;
                if (deadline == Clock::time_point::max()) { host.m_timers.erase(entry); }
                else if (auto ec = host.m_timers.insert_or_update(entry); ec and not host.m_error_code) { host.m_error_code = ec; }
            }
            return true;
        }, notification);
        if (accepted) { host.m_event_pump_p->wake(); }
    }
private:
    EventPump * m_event_pump_p;
    Entries m_entries;
    Timers m_timers;
    ReadyQueue m_ready_services;
    mutable std::mutex m_mutex;
    std::size_t m_active = 0;
    std::error_code m_error_code {};
    std::uint8_t m_lifecycle_flags = LifecycleFlags::eNone;
};

} // namespace lcf::async
