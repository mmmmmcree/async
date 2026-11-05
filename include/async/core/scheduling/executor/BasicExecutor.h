#pragma once

#include <async/core/queue/declarations/concepts.h>

LCF_ASYNC_EXPORT
namespace lcf::async {

template <work_queue_c Queue>
class BasicExecutor
{
public:
    ~BasicExecutor() noexcept = default;
    BasicExecutor() = delete;
    explicit BasicExecutor(Queue & queue) noexcept : m_queue_p(&queue) {}
public:
    bool executeOne() noexcept
    {
        auto * executable_p = m_queue_p->tryPop();
        const bool executed = executable_p;
        if (executed) { executable_p->execute(); }
        return executed;
    }
    bool waitAndExecuteOne() noexcept requires waiting_work_queue_c<Queue>
    {
        auto * executable_p = m_queue_p->waitPop();
        const bool executed = executable_p;
        if (executed) { executable_p->execute(); }
        return executed;
    }
    bool isEmpty() const noexcept { return m_queue_p->isEmpty(); }
private:
    Queue * m_queue_p;
};

} // namespace lcf::async
