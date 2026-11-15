#pragma once

#include <async/core/scheduling/declarations/enums.h>

#include <exception>
#include <expected>
#include <system_error>

namespace lcf::async::details {

inline PollResult require_poll_result(const std::expected<PollResult, std::error_code> & outcome) noexcept
{
    if (not outcome) { std::terminate(); }
    return *outcome;
}

template <typename SchedulingRuntime, typename ThreadSharedExecutor>
class WorkerPoller
{
    using DispatcherGroup = typename SchedulingRuntime::worker_dispatcher_group_t;
public:
    ~WorkerPoller() noexcept = default;
    WorkerPoller() = delete;
    WorkerPoller(SchedulingRuntime & runtime, ThreadSharedExecutor & executor) noexcept(
        noexcept(runtime.makeWorkerDispatcherGroup())) :
        m_runtime_p(&runtime), m_executor_p(&executor),
        m_dispatcher_group(runtime.makeWorkerDispatcherGroup()) {}
    WorkerPoller(const WorkerPoller &) = default;
    WorkerPoller(WorkerPoller &&) = default;
    WorkerPoller & operator=(const WorkerPoller &) = default;
    WorkerPoller & operator=(WorkerPoller &&) = default;
public:
    PollResult operator()() noexcept
    {
        return require_poll_result(m_runtime_p->pollWorker(m_dispatcher_group, *m_executor_p));
    }
private:
    SchedulingRuntime * m_runtime_p;
    ThreadSharedExecutor * m_executor_p;
    DispatcherGroup m_dispatcher_group;
};

} // namespace lcf::async::details
