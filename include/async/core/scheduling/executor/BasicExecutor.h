#pragma once

#include <async/core/queue/declarations/concepts.h>
#include <async/core/queue/utils/pop_one.h>

LCF_ASYNC_EXPORT
namespace lcf::async {

template <consumer_queue_c ExecutableStream>
requires executable_c<typename ExecutableStream::value_t>
class BasicExecutor
{
public:
    ~BasicExecutor() noexcept = default;
    BasicExecutor() = delete;
    explicit BasicExecutor(ExecutableStream & executable_stream) noexcept : m_executable_stream_p(&executable_stream) {}
    BasicExecutor(const BasicExecutor &) noexcept = default;
    BasicExecutor(BasicExecutor &&) noexcept = default;
    BasicExecutor & operator=(const BasicExecutor &) noexcept = default;
    BasicExecutor & operator=(BasicExecutor &&) noexcept = default;
public:
    bool executeOne() noexcept
    {
        auto * executable_p = queue::utils::pop_one(*m_executable_stream_p);
        const bool executed = executable_p;
        if (executed) { executable_p->execute(); }
        return executed;
    }
    bool waitAndExecuteOne() noexcept requires waiting_work_queue_c<ExecutableStream>
    {
        auto * executable_p = m_executable_stream_p->waitPop();
        const bool executed = executable_p;
        if (executed) { executable_p->execute(); }
        return executed;
    }
    bool isEmpty() const noexcept { return m_executable_stream_p->isEmpty(); }
private:
    ExecutableStream * m_executable_stream_p;
};

} // namespace lcf::async
