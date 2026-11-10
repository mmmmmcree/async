#pragma once

#include <async/core/queue/declarations/concepts.h>

LCF_ASYNC_EXPORT
namespace lcf::async {

template <executable_input_c ExecutableInput>
class BasicExecutor
{
public:
    ~BasicExecutor() noexcept = default;
    BasicExecutor() = delete;
    explicit BasicExecutor(ExecutableInput & executable_input) noexcept : m_executable_input_p(&executable_input) {}
public:
    bool executeOne() noexcept
    {
        auto * executable_p = m_executable_input_p->tryPop();
        const bool executed = executable_p;
        if (executed) { executable_p->execute(); }
        return executed;
    }
    bool waitAndExecuteOne() noexcept requires waiting_work_queue_c<ExecutableInput>
    {
        auto * executable_p = m_executable_input_p->waitPop();
        const bool executed = executable_p;
        if (executed) { executable_p->execute(); }
        return executed;
    }
    bool isEmpty() const noexcept { return m_executable_input_p->isEmpty(); }
private:
    ExecutableInput * m_executable_input_p;
};

} // namespace lcf::async
