#pragma once

#include <async/config/macros.h>
#include <async/core/driver/declarations/structs.h>

#include <atomic>
#include <chrono>
#include <semaphore>

LCF_ASYNC_EXPORT
namespace lcf::async::notify {

class SemaphoreEvent
{
public:
    ~SemaphoreEvent() noexcept = default;
    SemaphoreEvent() noexcept = default;
    SemaphoreEvent(const SemaphoreEvent &) = delete;
    SemaphoreEvent(SemaphoreEvent &&) = delete;
    SemaphoreEvent & operator=(const SemaphoreEvent &) = delete;
    SemaphoreEvent & operator=(SemaphoreEvent &&) = delete;
public:
    DriveReport driveOne() noexcept { return {}; }
    std::error_code waitUntil(std::chrono::steady_clock::time_point deadline) noexcept
    {
        bool acquired;
        if (deadline == std::chrono::steady_clock::time_point::max()) {
            m_semaphore.acquire();
            acquired = true;
        } else { acquired = m_semaphore.try_acquire_until(deadline); }
        // A timeout must leave a racing wake pending. Clear only after consuming its permit.
        if (acquired) { (void)m_pending.exchange(false, std::memory_order_acq_rel); }
        return {};
    }
    void wake() noexcept
    {
        // At most one outstanding permit represents all coalesced wakes.
        if (not m_pending.exchange(true, std::memory_order_acq_rel)) { m_semaphore.release(); }
    }
private:
    std::atomic<bool> m_pending {false};
    std::binary_semaphore m_semaphore {0};
};

} // namespace lcf::async::notify
