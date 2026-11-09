#pragma once

#include <async/config/macros.h>
#include <async/core/context/declarations/enums.h>

#include <chrono>
#include <cstdint>
#include <limits>

LCF_ASYNC_EXPORT
namespace lcf::async {

struct PollBudget
{
    std::uint64_t m_max_records = std::numeric_limits<std::uint64_t>::max();
    std::chrono::steady_clock::duration m_max_duration = std::chrono::steady_clock::duration::max();
};

struct PollReport
{
    constexpr PollResult getResult() const noexcept { return m_result; }
    constexpr bool hasProgress() const noexcept { return m_result != PollResult::eIdle; }

    std::uint64_t m_records = 0;
    std::chrono::steady_clock::duration m_elapsed {};
    bool m_count_limit_reached = false;
    bool m_time_limit_reached = false;
    PollResult m_result = PollResult::eIdle;
};

} // namespace lcf::async
