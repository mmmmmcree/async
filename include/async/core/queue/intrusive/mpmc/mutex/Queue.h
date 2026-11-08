#pragma once

#include <async/core/queue/ExecutionNode.h>
#include <async/core/queue/intrusive/mpmc/queue_declarations.h>

#include <condition_variable>
#include <mutex>
#include <system_error>

LCF_ASYNC_EXPORT
namespace lcf::async::queue::intrusive::mpmc {

template <typename Node>
class Queue<AlgorithmTag::eMutex, Node>
{
public:
    using Value = Node;
    using Executable = Node;
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
    std::error_code tryEnqueue(Node & node) noexcept
    {
        {
            std::lock_guard lock{m_mutex};
            if (m_closed) { return std::make_error_code(std::errc::operation_canceled); }
            node.markQueued();
            if (m_tail_p) { m_tail_p->setNext(&node); }
            else { m_head_p = &node; }
            m_tail_p = &node;
        }
        m_condition.notify_one();
        return {};
    }
    Node * tryPop() noexcept
    {
        std::lock_guard lock{m_mutex};
        return this->popFront();
    }
    Node * waitPop() noexcept
    {
        std::unique_lock lock{m_mutex};
        m_condition.wait(lock, [this] {
            return m_stop_requested || m_closed || m_head_p;
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
    bool isEmpty() const noexcept
    {
        std::lock_guard lock{m_mutex};
        return not m_head_p;
    }
private:
    Node * popFront() noexcept
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
    Node * m_head_p = nullptr;
    Node * m_tail_p = nullptr;
    bool m_closed = false;
    bool m_stop_requested = false;
};

} // namespace lcf::async::queue::intrusive::mpmc
