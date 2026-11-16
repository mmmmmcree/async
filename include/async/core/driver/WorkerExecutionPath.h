#pragma once

#include <async/core/dispatcher/DispatcherGroup.h>
#include <async/core/executor/declarations/structs.h>
#include <async/core/scheduling/declarations/concepts.h>

#include <span>
#include <system_error>
#include <utility>

LCF_ASYNC_EXPORT
namespace lcf::async {

template <request_dispatcher_c Dispatcher, work_executor_c Executor, typename SelectionPolicy, typename Coordinator>
requires selection_policy_c<SelectionPolicy, std::span<Dispatcher>>
class WorkerExecutionPath
{
    using DispatcherGroup = ::lcf::async::DispatcherGroup<Dispatcher, SelectionPolicy>;
public:
    ~WorkerExecutionPath() noexcept = default;
    WorkerExecutionPath() = delete;
    WorkerExecutionPath(std::span<Dispatcher> dispatchers, Executor & executor, Coordinator & coordinator,
        SelectionPolicy selection_policy = {}) :
        m_dispatchers(dispatchers, std::move(selection_policy)),
        m_executor_p(&executor), m_coordinator_p(&coordinator) {}
    WorkerExecutionPath(const WorkerExecutionPath &) = delete;
    WorkerExecutionPath(WorkerExecutionPath &&) = default;
    WorkerExecutionPath & operator=(const WorkerExecutionPath &) = delete;
    WorkerExecutionPath & operator=(WorkerExecutionPath &&) = delete;
public:
    ExecutionReport executeOne() noexcept
    {
        const DispatchReport dispatch_report = m_dispatchers.dispatchOne();
        if (dispatch_report.m_error_code) {
            m_coordinator_p->reportErrorCode(dispatch_report.m_error_code);
            return {.m_result = ExecutionResult::eStopped, .m_error_code = dispatch_report.m_error_code};
        }
        const ExecutionReport execution_report = m_executor_p->executeOne();
        if (execution_report.m_error_code) { return execution_report; }
        const bool progressed = dispatch_report.hasProgress() or execution_report.hasProgress();
        return {.m_result = progressed ? ExecutionResult::eProgress : ExecutionResult::eIdle,
            .m_executed = execution_report.m_executed};
    }
private:
    DispatcherGroup m_dispatchers;
    Executor * m_executor_p;
    Coordinator * m_coordinator_p;
};

} // namespace lcf::async
