#pragma once

#include <atomic>
#include <cassert>

namespace lcf::async {

class ScheduleNode
{
public:
    using ExecuteFunction = void (*)(void *) noexcept;

    ScheduleNode(void * state_p, ExecuteFunction execute_f) noexcept
        : m_state_p(state_p), m_execute_f(execute_f) {}

    ScheduleNode(const ScheduleNode &) = delete;
    ScheduleNode & operator=(const ScheduleNode &) = delete;
    ScheduleNode(ScheduleNode &&) = delete;
    ScheduleNode & operator=(ScheduleNode &&) = delete;

    ~ScheduleNode() noexcept { assert(not m_queued); }
    ScheduleNode * getNext() const noexcept
    {
        return m_next_p.load(std::memory_order_acquire);
    }
    void setNext(ScheduleNode * next_p) noexcept
    {
        m_next_p.store(next_p, std::memory_order_release);
    }
    bool isQueued() const noexcept { return m_queued; }
    void markQueued() noexcept
    {
        assert(not m_queued);
        m_queued = true;
    }
    void clearQueueState() noexcept
    {
        m_next_p.store(nullptr, std::memory_order_relaxed);
        m_queued = false;
    }
    void execute() noexcept
    {
        auto * state_p = m_state_p;
        auto execute_f = m_execute_f;
        // Completion may destroy this node. Do not access members after dispatch.
        execute_f(state_p);
    }
private:
    std::atomic<ScheduleNode *> m_next_p{nullptr};
    void * m_state_p;
    ExecuteFunction m_execute_f;
    bool m_queued = false;
};

} // namespace lcf::async
