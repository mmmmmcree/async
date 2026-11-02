#pragma once

#include <async/core/concepts.h>
#include "ScheduleNode.h"
#include <async/core/scheduling/queue/MutexInbox.h>

#include <cassert>
#include <concepts>
#include <cstddef>
#include <utility>

namespace lcf::async {

template <schedule_queue_c<ScheduleNode> Queue = MutexInbox>
class BasicEventLoop
{
public:
    template <typename... QueueArgs>
        requires std::constructible_from<Queue, QueueArgs...>
    explicit BasicEventLoop(QueueArgs &&... queue_args)
        : m_queue(std::forward<QueueArgs>(queue_args)...) {}

    BasicEventLoop(const BasicEventLoop &) = delete;
    BasicEventLoop & operator=(const BasicEventLoop &) = delete;

    ~BasicEventLoop() noexcept { assert(m_queue.empty()); }

    template <details::queue_record_c Record>
    EnqueueResult tryEnqueue(Record & record) noexcept
    {
        return m_queue.tryEnqueue(get_schedule_node(record));
    }

    bool pollOne() noexcept
    {
        auto node_p = m_queue.tryPop();
        if (node_p) { node_p->execute(); }
        return static_cast<bool>(node_p);
    }

    bool waitOne() noexcept
    requires blocking_schedule_queue_c<Queue, ScheduleNode>
    {
        auto node_p = m_queue.waitPop();
        if (node_p) { node_p->execute(); }
        return static_cast<bool>(node_p);
    }

    void requestStop() noexcept
        requires stoppable_schedule_queue_c<Queue, ScheduleNode>
    {
        m_queue.requestStop();
    }

    void close() noexcept { m_queue.close(); }
    bool empty() const noexcept { return m_queue.empty(); }
private:
    Queue m_queue;
};

template <event_loop_c EventLoop>
struct DefaultEventLoopFactory
{
    EventLoop makeControlLoop() const requires std::default_initializable<EventLoop>
    {
        return EventLoop{};
    }

    EventLoop makeWorkerLoop(std::size_t) const requires std::default_initializable<EventLoop>
    {
        return EventLoop{};
    }
};

} // namespace lcf::async
