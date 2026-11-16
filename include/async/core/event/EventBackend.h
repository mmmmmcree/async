#pragma once

#include <async/config/macros.h>
#include <async/core/event/declarations/concepts.h>

#include <condition_variable>
#include <mutex>

LCF_ASYNC_EXPORT
namespace lcf::async {

// Portable wake/timer backend. Platform backends add their own OS registration API.
class EventBackend
{
public:
    ~EventBackend() noexcept = default;
    EventBackend() noexcept = default;
    EventBackend(const EventBackend &) = delete;
    EventBackend(EventBackend &&) = delete;
    EventBackend & operator=(const EventBackend &) = delete;
    EventBackend & operator=(EventBackend &&) = delete;
public:
    DriveReport driveOne() noexcept { return {}; }
    std::error_code waitUntil(std::chrono::steady_clock::time_point deadline) noexcept
    {
        std::unique_lock lock {m_mutex};
        if (deadline == std::chrono::steady_clock::time_point::max()) {
            m_condition.wait(lock, [this] { return m_pending; });
        } else { m_condition.wait_until(lock, deadline, [this] { return m_pending; }); }
        m_pending = false;
        return {};
    }
    void wake() noexcept
    {
        { const std::lock_guard lock {m_mutex}; m_pending = true; }
        m_condition.notify_one();
    }
private:
    std::mutex m_mutex;
    std::condition_variable m_condition;
    bool m_pending = false;
};

} // namespace lcf::async
