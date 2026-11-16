#pragma once

#include <async/config/macros.h>
#include <async/core/executor/declarations/structs.h>

#include <stop_token>

LCF_ASYNC_EXPORT
namespace lcf::async {

template <typename Context, bool is_owner>
class ExecutionPort
{
public:
    ~ExecutionPort() noexcept = default;
    ExecutionPort() = delete;
    explicit ExecutionPort(Context & context, std::size_t index = 0) noexcept : m_context_p(&context), m_index(index) {}
    ExecutionPort(const ExecutionPort &) noexcept = default;
    ExecutionPort(ExecutionPort &&) noexcept = default;
    ExecutionPort & operator=(const ExecutionPort &) noexcept = default;
    ExecutionPort & operator=(ExecutionPort &&) noexcept = default;
public:
    ExecutionReport executeOne() noexcept { return m_context_p->template executeOne<is_owner>(m_index); }
    ExecutionReport waitAndExecuteOne(std::stop_token stop = {}) noexcept
    { return m_context_p->template waitAndExecuteOne<is_owner>(m_index, stop); }
private:
    Context * m_context_p;
    std::size_t m_index;
};

} // namespace lcf::async
