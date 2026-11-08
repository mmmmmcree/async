#pragma once

#include <async/config/macros.h>

#include <atomic>

LCF_ASYNC_EXPORT
namespace lcf::async {

class WorkItem
{
public:
    using ExecuteFunction = void (*)(void *) noexcept;
    using CancelFunction = void (*)(void *) noexcept;
public:
    ~WorkItem() noexcept = default;
    WorkItem() = delete;
    WorkItem(void * state_p, ExecuteFunction execute_f, CancelFunction cancel_f) noexcept :
        m_state_p(state_p), m_execute_f(execute_f), m_cancel_f(cancel_f) {}
    WorkItem(const WorkItem &) = delete;
    WorkItem & operator=(const WorkItem &) = delete;
    WorkItem(WorkItem &&) = delete;
    WorkItem & operator=(WorkItem &&) = delete;
public:
    WorkItem * getNext() const noexcept { return m_next_p.load(std::memory_order_acquire); }
    void setNext(WorkItem * next_p) noexcept { m_next_p.store(next_p, std::memory_order_release); }
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
    std::atomic<WorkItem *> m_next_p {nullptr};
    void * m_state_p;
    ExecuteFunction m_execute_f;
    CancelFunction m_cancel_f;
};

} // namespace lcf::async
