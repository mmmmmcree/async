#pragma once

#include <async/core/scheduling/details/queue.h>
#include <async/core/timer/details/timer.h>

namespace lcf::async::details {

class TimerService
{
    using Self = TimerService;
    using Clock = std::chrono::steady_clock;
    using EntryHandle = std::shared_ptr<TimerEntry>;
public:
    TimerService(Queue & queue, std::pmr::memory_resource * resource_p) : m_timers(resource_p), m_queue(queue) {}
    TimerService(const Self &) = delete;
    Self & operator=(const Self &) = delete;
public:
    bool submit(EntryHandle entry_handle) noexcept
    {
        return m_queue.synchronize([&](Queue::Transaction & transaction) noexcept {
            if (entry_handle->isDecided()) { return true; }
            if (transaction.isClosed()) { entry_handle->reject(); return false; }
            try {
                const auto first = m_timers.insert(std::move(entry_handle));
                this->publishDeadline();
                if (first) { transaction.wakeEvents(); }
            } catch (...) {
                entry_handle->setException(std::current_exception());
                transaction.push(entry_handle->claimCallback(entry_handle));
            }
            return true;
        });
    }
    bool cancel(EntryHandle entry_handle) noexcept
    {
        return m_queue.synchronize([&](Queue::Transaction & transaction) noexcept {
            if (entry_handle->isDecided()) { return true; }
            entry_handle->markCancelled();
            if (entry_handle->isQueued()) {
                m_timers.remove(entry_handle->getHeapIndex());
                this->publishDeadline();
            }
            if (transaction.isClosed()) { entry_handle->reject(); return false; }
            transaction.push(entry_handle->claimCallback(entry_handle));
            return true;
        });
    }
    EventPoll poll() noexcept
    {
        const auto deadline = m_deadline;
        if (deadline == Clock::time_point::max()) { return {}; }
        const auto now = Clock::now();
        if (deadline > now) { return {nullptr, deadline}; }
        auto entry_handle = m_timers.remove(0);
        auto * callback_p = entry_handle->claimCallback(entry_handle);
        return {callback_p, this->publishDeadline()};
    }
    std::size_t readyCount(Clock::time_point now) noexcept
    {
        if (m_deadline > now) { return 0; }
        return m_timers.readyCount(now);
    }
    void shutdown() noexcept
    {
        auto release = [&](Queue::Transaction &) noexcept { auto * callbacks_p = m_timers.release(); this->publishDeadline(); return callbacks_p; };
        auto * callbacks_p = m_queue.synchronize(release);
        while (callbacks_p) {
            auto * callback_p = callbacks_p;
            callbacks_p = callback_p->m_next_p;
            callback_p->abandon();
        }
    }
private:
    Clock::time_point publishDeadline() noexcept
    {
        return m_deadline = m_timers.empty() ? Clock::time_point::max() : m_timers.deadline();
    }
private:
    TimerQueue m_timers;
    Queue & m_queue;
    Clock::time_point m_deadline = Clock::time_point::max();
};

inline std::shared_ptr<Queue> make_timed_queue(std::pmr::memory_resource * resource_p)
{
    return std::make_shared<Queue>([resource_p](Queue & queue) {
        return EventSource {std::make_unique<TimerService>(queue, resource_p)};
    });
}

}
