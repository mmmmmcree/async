#pragma once

#include <async/core/enums.h>
#include <async/core/queue/intrusive/ExecutionNode.h>
#include <async/core/queue/intrusive/mpmc/queue_declarations.h>

#include <cassert>
#include <condition_variable>
#include <mutex>

namespace lcf::async::queue::intrusive::mpmc {

template <>
class Queue<AlgorithmTag::eMutex>
{
public:
    using Executable = ExecutionNode;
    static constexpr bool k_supports_multiple_producers = true;
    static constexpr bool k_supports_multiple_consumers = true;
public:
    ~Queue() noexcept = default;
    Queue() = default;
    Queue(const Queue &) = delete;
    Queue(Queue &&) = delete;
    Queue & operator=(const Queue &) = delete;
    Queue & operator=(Queue &&) = delete;
public:
    EnqueueResult tryEnqueue(ExecutionNode & node) noexcept
    {
        {
            std::lock_guard lock{m_mutex};
            if (m_closed) { return EnqueueResult::eClosed; }
            node.markQueued();
            if (m_tail_p) { m_tail_p->setNext(&node); }
            else { m_head_p = &node; }
            m_tail_p = &node;
        }
        m_condition.notify_one();
        return EnqueueResult::eAccepted;
    }
    ExecutionNode * tryPop() noexcept
    {
        std::lock_guard lock{m_mutex};
        return this->popFront();
    }
    ExecutionNode * waitPop() noexcept
    {
        std::unique_lock lock{m_mutex};
        m_condition.wait(lock, [this] {
            return m_stop_requested || m_closed || m_head_p != nullptr;
        });
        if (m_stop_requested) { return nullptr; }
        return this->popFront();
    }
    void requestStop() noexcept
    {
        {
            std::lock_guard lock{m_mutex};
            m_stop_requested = true;
        }
        m_condition.notify_all();
    }

    void close() noexcept
    {
        {
            std::lock_guard lock{m_mutex};
            m_closed = true;
        }
        m_condition.notify_all();
    }
    bool empty() const noexcept
    {
        std::lock_guard lock{m_mutex};
        return m_head_p == nullptr;
    }
private:
    ExecutionNode * popFront() noexcept
    {
        auto * node_p = m_head_p;
        if (not node_p) { return nullptr; }
        m_head_p = node_p->getNext();
        if (not m_head_p) { m_tail_p = nullptr; }
        node_p->clearQueueState();
        return node_p;
    }
private:
    mutable std::mutex m_mutex;
    std::condition_variable m_condition;
    ExecutionNode * m_head_p = nullptr;
    ExecutionNode * m_tail_p = nullptr;
    bool m_closed = false;
    bool m_stop_requested = false;
};

} // namespace lcf::async::queue::intrusive::mpmc
