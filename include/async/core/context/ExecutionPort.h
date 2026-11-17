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
    ExecutionReport executeOne() noexcept
    {
        if constexpr (is_owner) { return m_context_p->executeOne(); }
        else { return m_context_p->executeOne(m_index); }
    }
    ExecutionReport waitAndExecuteOne(std::stop_token stop = {}) noexcept
    {
        const std::stop_callback wake {stop, [this] { m_context_p->m_signal.notifyAll(); }};
        while (true) {
            const auto observed = m_context_p->m_signal.observe();
            if (stop.stop_requested()) { return ExecutionReport::make_from_stopped(); }
            const ExecutionReport report = this->executeOne();
            if (report.getExecutedCount() or report.isStopped() or report.isFailed()) { return report; }
            if (report.isSucceeded()) { continue; }
            if constexpr (is_owner) {
                if (const std::error_code ec = m_context_p->waitForEvents()) { return ExecutionReport::make_from_error_code(ec); }
            } else { m_context_p->m_signal.wait(observed); }
        }
    }
private:
    Context * m_context_p;
    std::size_t m_index;
};

} // namespace lcf::async
