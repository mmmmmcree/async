#pragma once

#include <async/config/macros.h>

#include <atomic>

LCF_ASYNC_EXPORT
namespace lcf::async::queue {

class ExecutionNode
{
public:
    using ExecuteFunction = void (*)(void *) noexcept;
    using CancelFunction = void (*)(void *) noexcept;
public:
    ~ExecutionNode() noexcept = default;
    ExecutionNode() = delete;
    ExecutionNode(void * state_p, ExecuteFunction execute_f, CancelFunction cancel_f) noexcept :
        m_state_p(state_p), m_execute_f(execute_f), m_cancel_f(cancel_f) {}
    ExecutionNode(const ExecutionNode &) = delete;
    ExecutionNode & operator=(const ExecutionNode &) = delete;
    ExecutionNode(ExecutionNode &&) = delete;
    ExecutionNode & operator=(ExecutionNode &&) = delete;
public:
    ExecutionNode * getNext() const noexcept { return m_next_p.load(std::memory_order_acquire); }
    void setNext(ExecutionNode * next_p) noexcept { m_next_p.store(next_p, std::memory_order_release); }
    void execute() noexcept
    {
        auto * state_p = m_state_p;
        auto execute_f = m_execute_f;
        execute_f(state_p);
    }
    // Dequeue and exclude execution before cancellation; either callback may destroy this node.
    void cancel() noexcept
    {
        auto * state_p = m_state_p;
        auto cancel_f = m_cancel_f;
        cancel_f(state_p);
    }
private:
    std::atomic<ExecutionNode *> m_next_p {nullptr};
    void * m_state_p;
    ExecuteFunction m_execute_f;
    CancelFunction m_cancel_f;
};

} // namespace lcf::async::queue
