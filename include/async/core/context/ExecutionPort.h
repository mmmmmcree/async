#pragma once

#include <async/core/scheduling/declarations/poll_structs.h>
#include <async/core/notify/declarations/concepts.h>
#include <async/core/scheduling/declarations/concepts.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>

LCF_ASYNC_EXPORT
namespace lcf::async {

// Views of the same target share one designated consumer thread.
template <work_executor_c TargetExecutor, notify::notifier_c Notifier>
class ExecutionPort
{
public:
    ExecutionPort(TargetExecutor & target_executor, std::atomic<std::uint64_t> & active_poll_count,
        Notifier & notifier) noexcept :
        m_target_executor_p(&target_executor), m_active_poll_count_p(&active_poll_count), m_notifier_p(&notifier) {}

    PollReport pollOne() const noexcept
    {
        m_active_poll_count_p->fetch_add(1, std::memory_order_acq_rel);
        PollReport report;
        report.m_records = m_target_executor_p->executeOne();
        report.m_result = report.m_records ? PollResult::eExecuted : PollResult::eIdle;
        if (report.hasProgress()) { m_notifier_p->notifyAll(); }
        m_active_poll_count_p->fetch_sub(1, std::memory_order_release);
        return report;
    }

    PollReport poll(PollBudget budget = {}) const noexcept
    {
        using Clock = std::chrono::steady_clock;
        const Clock::time_point started = Clock::now();
        PollReport report;
        while (report.m_records < budget.m_max_records and Clock::now() - started < budget.m_max_duration) {
            const PollReport polled = this->pollOne();
            report.m_records += polled.m_records;
            report.m_result = std::max(report.m_result, polled.getResult());
            if (not polled.hasProgress()) { break; }
        }
        report.m_elapsed = Clock::now() - started;
        report.m_count_limit_reached = report.m_records == budget.m_max_records;
        report.m_time_limit_reached = report.m_elapsed >= budget.m_max_duration;
        return report;
    }
private:
    TargetExecutor * m_target_executor_p;
    std::atomic<std::uint64_t> * m_active_poll_count_p;
    Notifier * m_notifier_p;
};

} // namespace lcf::async
