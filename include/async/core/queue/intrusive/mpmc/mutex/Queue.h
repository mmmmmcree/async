#pragma once

#include <async/core/queue/intrusive/Queue.h>
#include <async/core/queue/intrusive/mpmc/queue_declarations.h>

#include <condition_variable>
#include <mutex>
#include <system_error>

LCF_ASYNC_EXPORT
namespace lcf::async::queue::intrusive::mpmc {

template <linked_node_c Node>
class Queue<Node, AlgorithmTag::eMutex>
{
    using NodeQueue = queue::intrusive::Queue<Node>;
public:
    using Value = Node;
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
            m_queue.pushBack(node);
        }
        m_condition.notify_one();
        return {};
    }
    Node * tryPop() noexcept
    {
        std::lock_guard lock{m_mutex};
        return m_queue.tryPop();
    }
    Node * waitPop() noexcept
    {
        std::unique_lock lock{m_mutex};
        m_condition.wait(lock, [this] {
            return m_stop_requested or m_closed or not m_queue.isEmpty();
        });
        if (m_stop_requested) { return nullptr; }
        return m_queue.tryPop();
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
        return m_queue.isEmpty();
    }
private:
    mutable std::mutex m_mutex;
    std::condition_variable m_condition;
    NodeQueue m_queue;
    bool m_closed = false;
    bool m_stop_requested = false;
};

} // namespace lcf::async::queue::intrusive::mpmc
