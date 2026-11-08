#pragma once

#include <async/config/macros.h>

#include <atomic>

LCF_ASYNC_EXPORT
namespace lcf::async::queue::intrusive {

class ExecutionNode
{
public:
    using ExecuteFunction = void (*)(void *) noexcept;
public:
    ~ExecutionNode() noexcept = default;
    ExecutionNode() = delete;
    ExecutionNode(void * state_p, ExecuteFunction execute_f) noexcept :
        m_state_p(state_p), m_execute_f(execute_f) {}
    ExecutionNode(const ExecutionNode &) = delete;
    ExecutionNode & operator=(const ExecutionNode &) = delete;
    ExecutionNode(ExecutionNode &&) = delete;
    ExecutionNode & operator=(ExecutionNode &&) = delete;
public:
    ExecutionNode * getNext() const noexcept { return m_next_p.load(std::memory_order_acquire); }
    void setNext(ExecutionNode * next_p) noexcept { m_next_p.store(next_p, std::memory_order_release); }
    bool isQueued() const noexcept { return m_queued; }
    void markQueued() noexcept { m_queued = true; }
    void clearQueueState() noexcept
    {
        m_next_p.store(nullptr, std::memory_order_relaxed);
        m_queued = false;
    }
    void execute() noexcept
    {
        auto * state_p = m_state_p;
        auto execute_f = m_execute_f;
        execute_f(state_p);
    }
private:
    std::atomic<ExecutionNode *> m_next_p {nullptr};
    void * m_state_p;
    ExecuteFunction m_execute_f;
    bool m_queued = false;
};

} // namespace lcf::async::queue::intrusive
