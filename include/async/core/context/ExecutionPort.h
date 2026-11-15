#pragma once

#include <async/core/details/concepts/constructible.h>
#include <async/core/scheduling/declarations/poll_structs.h>
#include <async/core/scheduling/declarations/concepts.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <utility>

LCF_ASYNC_EXPORT
namespace lcf::async {

// Views of the same target share one designated consumer thread.
template <work_poller_c Poller>
class ExecutionPort
{
public:
    ~ExecutionPort() noexcept = default;
    ExecutionPort() = delete;
    explicit ExecutionPort(Poller poller) noexcept(details::all_nothrow_move_constructible<Poller>) :
        m_poller(std::move(poller)) {}
    ExecutionPort(const ExecutionPort &) = default;
    ExecutionPort(ExecutionPort &&) = default;
    ExecutionPort & operator=(const ExecutionPort &) = default;
    ExecutionPort & operator=(ExecutionPort &&) = default;
public:
    PollReport pollOne() noexcept
    {
        const PollResult result = m_poller();
        return {.m_records = result == PollResult::eExecuted, .m_result = result};
    }
    PollReport poll(PollBudget budget = {}) noexcept
    {
        using Clock = std::chrono::steady_clock;
        const Clock::time_point started = Clock::now();
        std::uint64_t executed_count = 0;
        PollResult result = PollResult::eIdle;
        while (executed_count < budget.m_max_records and Clock::now() - started < budget.m_max_duration) {
            const PollResult polled = m_poller();
            executed_count += polled == PollResult::eExecuted;
            result = std::max(result, polled);
            if (not polled) { break; }
        }
        const Clock::duration elapsed = Clock::now() - started;
        return {
            .m_records = executed_count,
            .m_elapsed = elapsed,
            .m_count_limit_reached = executed_count == budget.m_max_records,
            .m_time_limit_reached = elapsed >= budget.m_max_duration,
            .m_result = result
        };
    }
private:
    Poller m_poller;
};

} // namespace lcf::async
