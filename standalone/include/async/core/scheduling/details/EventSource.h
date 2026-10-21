#pragma once

#include <async/core/scheduling/details/callback.h>
#include <chrono>
#include <concepts>
#include <memory>
#include <utility>

namespace lcf::async::details {

/* poll/readyCount run under the context transaction; shutdown runs outside it.
 * A producer updates source state and wakes the pump in the same transaction. */
struct EventPoll
{
    Callback * m_callback_p = nullptr;
    std::chrono::steady_clock::time_point m_deadline = std::chrono::steady_clock::time_point::max();
};

template <typename Source>
concept event_source_c = requires(Source & source, std::chrono::steady_clock::time_point now) {
    { source.poll() } noexcept -> std::same_as<EventPoll>;
    { source.readyCount(now) } noexcept -> std::same_as<std::size_t>;
    { source.shutdown() } noexcept;
};

class EventSource
{
    using Self = EventSource;
    using Owner = std::unique_ptr<void, void (*)(void *) noexcept>;
    struct Operations
    {
        EventPoll (*m_poll)(void *) noexcept;
        std::size_t (*m_ready_count)(void *, std::chrono::steady_clock::time_point) noexcept;
        void (*m_shutdown)(void *) noexcept;
    };
public:
    EventSource() noexcept = default;
    template <event_source_c Source>
    explicit EventSource(std::unique_ptr<Source> source_up) noexcept :
        m_owner_up(source_up.release(), [](void * owner_p) noexcept { delete static_cast<Source *>(owner_p); }), m_operations_p(&k_operations<Source>) {}
    EventSource(const Self &) = delete;
    Self & operator=(const Self &) = delete;
    EventSource(Self &&) noexcept = default;
    Self & operator=(Self &&) noexcept = default;
public:
    explicit operator bool() const noexcept { return static_cast<bool>(m_owner_up); }
    EventPoll poll() const noexcept { return m_operations_p->m_poll(m_owner_up.get()); }
    std::size_t readyCount(std::chrono::steady_clock::time_point now) const noexcept { return m_operations_p->m_ready_count(m_owner_up.get(), now); }
    void shutdown() const noexcept { m_operations_p->m_shutdown(m_owner_up.get()); }
    template <event_source_c Source>
    Source * target() const noexcept
    {
        if (m_operations_p != &k_operations<Source>) { return nullptr; }
        return static_cast<Source *>(m_owner_up.get());
    }
private:
    template <event_source_c Source>
    static inline constexpr Operations k_operations {
        [](void * owner_p) noexcept { return static_cast<Source *>(owner_p)->poll(); },
        [](void * owner_p, std::chrono::steady_clock::time_point now) noexcept { return static_cast<Source *>(owner_p)->readyCount(now); },
        [](void * owner_p) noexcept { static_cast<Source *>(owner_p)->shutdown(); }};
    Owner m_owner_up {nullptr, nullptr};
    const Operations * m_operations_p = nullptr;
};

}
