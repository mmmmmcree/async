#pragma once

#include <async/core/dispatcher/DispatcherGroup.h>
#include <async/core/executor/declarations/structs.h>
#include <async/core/scheduling/declarations/concepts.h>

#include <span>
#include <utility>

LCF_ASYNC_EXPORT
namespace lcf::async {

template <request_dispatcher_c Dispatcher, work_executor_c Executor, typename SelectionPolicy>
requires selection_policy_c<SelectionPolicy, std::span<Dispatcher>>
class DispatchingExecutor
{
    using DispatcherGroup = ::lcf::async::DispatcherGroup<Dispatcher, SelectionPolicy>;
public:
    ~DispatchingExecutor() noexcept = default;
    DispatchingExecutor() = delete;
    DispatchingExecutor(std::span<Dispatcher> dispatchers, Executor & executor, SelectionPolicy selection_policy = {}) :
        m_dispatchers(dispatchers, std::move(selection_policy)), m_executor_p(&executor) {}
    DispatchingExecutor(const DispatchingExecutor &) = delete;
    DispatchingExecutor(DispatchingExecutor &&) = default;
    DispatchingExecutor & operator=(const DispatchingExecutor &) = delete;
    DispatchingExecutor & operator=(DispatchingExecutor &&) = delete;
public:
    ExecutionReport executeOne() noexcept
    {
        const DispatchReport dispatch_report = m_dispatchers.dispatchOne();
        if (const std::error_code ec = dispatch_report.getErrorCode()) {
            return ExecutionReport::make_from_error_code(ec);
        }
        const ExecutionReport execution_report = m_executor_p->executeOne();
        if (execution_report.isFailed() or execution_report.isStopped()) { return execution_report; }
        const bool succeeded = dispatch_report.hasProgress() or execution_report.isSucceeded();
        return succeeded ? ExecutionReport::make_from_succeeded(execution_report.getExecutedCount()) : ExecutionReport {};
    }
private:
    DispatcherGroup m_dispatchers;
    Executor * m_executor_p;
};

} // namespace lcf::async
