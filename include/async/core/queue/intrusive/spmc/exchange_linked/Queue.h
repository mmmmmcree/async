#pragma once

#include <async/core/queue/intrusive/spmc/queue_declarations.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <mutex>
#include <system_error>
#include <type_traits>
#include <utility>

LCF_ASYNC_EXPORT
namespace lcf::async::queue::intrusive::spmc {

// Node links must synchronize concurrent producer and consumer access.
template <linked_node_c Node>
class Queue<Node, AlgorithmTag::eExchangeLinked>
{
public:
    using Value = Node;
    static constexpr bool k_supports_multiple_producers = false;
    static constexpr bool k_supports_multiple_consumers = true;

    static_assert(std::atomic<Node *>::is_always_lock_free,
        "Exchange-linked queue requires lock-free pointer atomics");
    static_assert(std::atomic<std::uint64_t>::is_always_lock_free,
        "Exchange-linked queue requires lock-free 64-bit atomics");
    static_assert(std::atomic<unsigned>::is_always_lock_free,
        "Exchange-linked queue requires lock-free unsigned atomics");
    static_assert(std::atomic<std::size_t>::is_always_lock_free,
        "Exchange-linked queue requires lock-free size atomics");
private:
    static constexpr unsigned k_closed_bit = 1u;
    static constexpr unsigned k_producer_active_bit = 2u;
public:
    ~Queue() noexcept = default;
    Queue() noexcept(std::is_nothrow_default_constructible_v<Node>)
    requires std::default_initializable<Node> :
        m_stub {} { m_stub.setNext(nullptr); }
    template <typename... Arguments>
    requires std::constructible_from<Node, Arguments...>
    explicit Queue(std::in_place_t, Arguments &&... arguments) noexcept(std::is_nothrow_constructible_v<Node, Arguments...>) :
        m_stub(std::forward<Arguments>(arguments)...) { m_stub.setNext(nullptr); }
    Queue(const Queue &) = delete;
    Queue(Queue &&) = delete;
    Queue & operator=(const Queue &) = delete;
    Queue & operator=(Queue &&) = delete;
public:
    std::error_code tryEnqueue(Node & node) noexcept
    {
        auto state = m_lifecycle.load(std::memory_order_acquire);
        if ((state & k_closed_bit) != 0) { return std::make_error_code(std::errc::operation_canceled); }
        if ((state & k_producer_active_bit) != 0) { std::terminate(); }
        if (not m_lifecycle.compare_exchange_strong(
                state, state | k_producer_active_bit,
                std::memory_order_acq_rel, std::memory_order_acquire)) {
            if ((state & k_closed_bit) != 0) { return std::make_error_code(std::errc::operation_canceled); }
            std::terminate();
        }
#if defined(LCF_ASYNC_ENABLE_TEST_HOOKS)
        if (m_after_producer_entry_hook_f) {
            m_after_producer_entry_hook_f(m_after_producer_entry_hook_state_p);
        }
#endif
        node.setNext(nullptr);
        m_queued_count.fetch_add(1, std::memory_order_relaxed);
        auto * previous_p = m_head_p.exchange(&node, std::memory_order_acq_rel);
        previous_p->setNext(&node);
        m_wake_sequence.fetch_add(1, std::memory_order_release);
        m_wake_sequence.notify_one();
        m_lifecycle.fetch_and(~k_producer_active_bit, std::memory_order_release);
        m_lifecycle.notify_all();
        return {};
    }
    Node * tryPop() noexcept
    {
        std::lock_guard lock{m_consumer_mutex};
#if defined(LCF_ASYNC_ENABLE_TEST_HOOKS)
        if (m_after_consumer_lock_hook_f) {
            m_after_consumer_lock_hook_f(m_after_consumer_lock_hook_state_p);
        }
#endif
        return this->popFront();
    }
    Node * waitPop() noexcept
    {
        for (;;) {
            if (m_stop_requested.load(std::memory_order_acquire)) { return nullptr; }
            if (auto * node_p = this->tryPop()) { return node_p; }
            if (this->isClosedAndQuiescent() and this->isEmpty()) { return nullptr; }
            const auto observed = m_wake_sequence.load(std::memory_order_acquire);
            if (m_stop_requested.load(std::memory_order_acquire)) { return nullptr; }
            if (auto * node_p = this->tryPop()) { return node_p; }
            if (this->isClosedAndQuiescent() and this->isEmpty()) { return nullptr; }
            m_wake_sequence.wait(observed, std::memory_order_acquire);
        }
    }
    void requestStop() noexcept
    {
        m_stop_requested.store(true, std::memory_order_release);
        this->notifyWaiters();
    }
    void close() noexcept
    {
        auto state = m_lifecycle.fetch_or(k_closed_bit, std::memory_order_acq_rel);
        while ((state & k_producer_active_bit) != 0) {
            m_lifecycle.wait(state, std::memory_order_acquire);
            state = m_lifecycle.load(std::memory_order_acquire);
        }
        this->notifyWaiters();
    }
    bool isEmpty() const noexcept
    {
        return m_queued_count.load(std::memory_order_relaxed) == 0;
    }
private:
#if defined(LCF_ASYNC_ENABLE_TEST_HOOKS)
    friend struct QueueTestAccess;
    using TestHook = void (*)(void *) noexcept;
    TestHook m_after_consumer_lock_hook_f = nullptr;
    void * m_after_consumer_lock_hook_state_p = nullptr;
    TestHook m_after_producer_entry_hook_f = nullptr;
    void * m_after_producer_entry_hook_state_p = nullptr;
#endif
private:
    Node * popFront() noexcept
    {
        auto * tail_p = m_tail_p;
        auto * next_p = tail_p->getNext();
        if (tail_p == &m_stub) {
            if (not next_p) { return nullptr; }
            m_tail_p = next_p;
            tail_p = next_p;
            next_p = tail_p->getNext();
        }
        if (next_p) {
            m_tail_p = next_p;
            return this->releaseNode(*tail_p);
        }
        if (tail_p != m_head_p.load(std::memory_order_acquire)) { return nullptr; }
        m_stub.setNext(nullptr);
        auto * previous_p = m_head_p.exchange(&m_stub, std::memory_order_acq_rel);
        previous_p->setNext(&m_stub);
        next_p = tail_p->getNext();
        if (next_p) {
            m_tail_p = next_p;
            return this->releaseNode(*tail_p);
        }
        return nullptr;
    }
    Node * releaseNode(Node & node) noexcept
    {
        node.setNext(nullptr);
        m_queued_count.fetch_sub(1, std::memory_order_relaxed);
        return &node;
    }
    bool isClosedAndQuiescent() const noexcept
    {
        return m_lifecycle.load(std::memory_order_acquire) == k_closed_bit;
    }
    void notifyWaiters() noexcept
    {
        m_wake_sequence.fetch_add(1, std::memory_order_release);
        m_wake_sequence.notify_all();
    }
private:
    mutable std::mutex m_consumer_mutex;
    Node m_stub;
    std::atomic<Node *> m_head_p {&m_stub};
    Node * m_tail_p = &m_stub;
    std::atomic<std::size_t> m_queued_count{0};
    std::atomic<std::uint64_t> m_wake_sequence{0};
    std::atomic<unsigned> m_lifecycle{0};
    std::atomic<bool> m_stop_requested{false};
};

} // namespace lcf::async::queue::intrusive::spmc
