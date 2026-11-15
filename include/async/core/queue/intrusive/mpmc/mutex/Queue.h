#pragma once

#include <async/core/queue/intrusive/Queue.h>
#include <async/core/queue/intrusive/mpmc/queue_declarations.h>

#include <mutex>
#include <system_error>

LCF_ASYNC_EXPORT
namespace lcf::async::queue::intrusive::mpmc {

template <linked_node_c Node>
class Queue<Node, AlgorithmTag::eMutex>
{
    using NodeQueue = queue::intrusive::Queue<Node>;
public:
    using value_t = Node;
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
    std::error_code enqueue(Node & node) noexcept
    {
        {
            std::lock_guard lock{m_mutex};
            if (m_closed) { return std::make_error_code(std::errc::operation_canceled); }
            m_queue.pushBack(node);
        }
        return {};
    }
    Node * pop() noexcept
    {
        std::lock_guard lock{m_mutex};
        return m_queue.pop();
    }
    void close() noexcept
    {
        {
            std::lock_guard lock{m_mutex};
            m_closed = true;
        }
    }
    bool isEmpty() const noexcept
    {
        std::lock_guard lock{m_mutex};
        return m_queue.isEmpty();
    }
private:
    mutable std::mutex m_mutex;
    NodeQueue m_queue;
    bool m_closed = false;
};

} // namespace lcf::async::queue::intrusive::mpmc
