#pragma once

#include <async/core/operations.h>

#include <cassert>
#include <concepts>

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
    ScheduleNode * getNext() const noexcept { return m_next_p; }
    void setNext(ScheduleNode * next_p) noexcept { m_next_p = next_p; }
    bool isQueued() const noexcept { return m_queued; }
    void markQueued() noexcept
    {
        assert(not m_queued);
        m_queued = true;
    }
    void clearQueueState() noexcept
    {
        m_next_p = nullptr;
        m_queued = false;
    }
    void * getState() const noexcept { return m_state_p; }
    ExecuteFunction getExecuteFunction() const noexcept { return m_execute_f; }
private:
    ScheduleNode * m_next_p = nullptr;
    void * m_state_p;
    ExecuteFunction m_execute_f;
    bool m_queued = false;
};

namespace details {

template <typename Record>
concept queue_record_c = schedule_record_c<Record>
    && requires(Record & record) {
        { get_schedule_node(record) } noexcept
            -> std::same_as<ScheduleNode &>;
};

} // namespace details

} // namespace lcf::async
