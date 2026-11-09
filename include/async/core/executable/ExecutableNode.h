#pragma once

#include <async/config/macros.h>

#include <atomic>

LCF_ASYNC_EXPORT
namespace lcf::async {

class ExecutableNode
{
public:
    using ExecuteFunction = void (*)(void *) noexcept;
    using CancelFunction = void (*)(void *) noexcept;
public:
    ~ExecutableNode() noexcept = default;
    ExecutableNode() = delete;
    ExecutableNode(void * state_p, ExecuteFunction execute_f, CancelFunction cancel_f) noexcept :
        m_state_p(state_p), m_execute_f(execute_f), m_cancel_f(cancel_f) {}
    ExecutableNode(const ExecutableNode &) = delete;
    ExecutableNode & operator=(const ExecutableNode &) = delete;
    ExecutableNode(ExecutableNode &&) = delete;
    ExecutableNode & operator=(ExecutableNode &&) = delete;
public:
    ExecutableNode * getNext() const noexcept { return m_next_p.load(std::memory_order_acquire); }
    void setNext(ExecutableNode * next_p) noexcept { m_next_p.store(next_p, std::memory_order_release); }
    void execute() noexcept
    {
        auto * state_p = m_state_p;
        auto execute_f = m_execute_f;
        execute_f(state_p);
    }
    void cancel() noexcept
    {
        auto * state_p = m_state_p;
        auto cancel_f = m_cancel_f;
        cancel_f(state_p);
    }
private:
    std::atomic<ExecutableNode *> m_next_p {nullptr};
    void * m_state_p;
    ExecuteFunction m_execute_f;
    CancelFunction m_cancel_f;
};

} // namespace lcf::async
