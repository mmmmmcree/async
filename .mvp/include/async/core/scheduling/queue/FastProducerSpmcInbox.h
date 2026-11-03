#pragma once

#include <async/core/enums.h>
#include <async/core/scheduling/ScheduleNode.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <mutex>

namespace lcf::async {

class FastProducerSpmcInbox
{
public:
    using Node = ScheduleNode;
    static constexpr bool supports_multiple_producers = false;
    static constexpr bool supports_multiple_consumers = true;

    static_assert(std::atomic<Node *>::is_always_lock_free,
        "FastProducerSpmcInbox requires lock-free pointer atomics");
    static_assert(std::atomic<std::uint64_t>::is_always_lock_free,
        "FastProducerSpmcInbox requires lock-free 64-bit atomics");
    static_assert(std::atomic<unsigned>::is_always_lock_free,
        "FastProducerSpmcInbox requires lock-free unsigned atomics");
    static_assert(std::atomic<std::size_t>::is_always_lock_free,
        "FastProducerSpmcInbox requires lock-free size atomics");

    FastProducerSpmcInbox() noexcept
        : m_stub{nullptr, &execute_stub}, m_head_p{&m_stub}, m_tail_p(&m_stub) {}

    FastProducerSpmcInbox(const FastProducerSpmcInbox &) = delete;
    FastProducerSpmcInbox & operator=(const FastProducerSpmcInbox &) = delete;

    EnqueueResult tryEnqueue(Node & node) noexcept
    {
        auto state = m_lifecycle.load(std::memory_order_acquire);
        if ((state & k_closed_bit) != 0) {
            return EnqueueResult::eClosed;
        }
        if ((state & k_producer_active_bit) != 0) {
            std::terminate();
        }
        if (not m_lifecycle.compare_exchange_strong(
                state, state | k_producer_active_bit,
                std::memory_order_acq_rel, std::memory_order_acquire)) {
            if ((state & k_closed_bit) != 0) {
                return EnqueueResult::eClosed;
            }
            std::terminate();
        }
#if defined(LCF_ASYNC_ENABLE_TEST_HOOKS)
        if (m_after_producer_entry_hook_f) {
            m_after_producer_entry_hook_f(m_after_producer_entry_hook_state_p);
        }
#endif
        node.markQueued();
        node.setNext(nullptr);
        m_queued_count.fetch_add(1, std::memory_order_relaxed);

        auto * previous_p = m_head_p.exchange(&node, std::memory_order_acq_rel);
        previous_p->setNext(&node);

        m_wake_sequence.fetch_add(1, std::memory_order_release);
        m_wake_sequence.notify_one();

        m_lifecycle.fetch_and(~k_producer_active_bit, std::memory_order_release);
        m_lifecycle.notify_all();
        return EnqueueResult::eAccepted;
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
            if (m_stop_requested.load(std::memory_order_acquire)) {
                return nullptr;
            }
            if (auto * node_p = this->tryPop()) {
                return node_p;
            }
            if (this->isClosedAndQuiescent() and this->empty()) {
                return nullptr;
            }

            const auto observed = m_wake_sequence.load(std::memory_order_acquire);
            if (m_stop_requested.load(std::memory_order_acquire)) {
                return nullptr;
            }
            if (auto * node_p = this->tryPop()) {
                return node_p;
            }
            if (this->isClosedAndQuiescent() and this->empty()) {
                return nullptr;
            }
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

    bool empty() const noexcept
    {
        return m_queued_count.load(std::memory_order_relaxed) == 0;
    }

private:
#if defined(LCF_ASYNC_ENABLE_TEST_HOOKS)
    friend struct FastProducerSpmcInboxTestAccess;
    using TestHook = void (*)(void *) noexcept;
    TestHook m_after_consumer_lock_hook_f = nullptr;
    void * m_after_consumer_lock_hook_state_p = nullptr;
    TestHook m_after_producer_entry_hook_f = nullptr;
    void * m_after_producer_entry_hook_state_p = nullptr;
#endif

    static constexpr unsigned k_closed_bit = 1u;
    static constexpr unsigned k_producer_active_bit = 2u;

    static void execute_stub(void *) noexcept {}

    Node * popFront() noexcept
    {
        auto * tail_p = m_tail_p;
        auto * next_p = tail_p->getNext();

        if (tail_p == &m_stub) {
            if (not next_p) {
                return nullptr;
            }
            m_tail_p = next_p;
            tail_p = next_p;
            next_p = tail_p->getNext();
        }

        if (next_p) {
            m_tail_p = next_p;
            return this->releaseNode(*tail_p);
        }

        if (tail_p != m_head_p.load(std::memory_order_acquire)) {
            return nullptr;
        }

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
        node.clearQueueState();
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

    mutable std::mutex m_consumer_mutex;
    Node m_stub;
    std::atomic<Node *> m_head_p;
    Node * m_tail_p;
    std::atomic<std::size_t> m_queued_count{0};
    std::atomic<std::uint64_t> m_wake_sequence{0};
    std::atomic<unsigned> m_lifecycle{0};
    std::atomic<bool> m_stop_requested{false};
};

} // namespace lcf::async
