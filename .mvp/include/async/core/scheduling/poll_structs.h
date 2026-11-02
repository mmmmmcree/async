#pragma once

#include <chrono>
#include <cstddef>
#include <limits>

namespace lcf::async {

struct PollBudget
{
    std::size_t m_max_records = std::numeric_limits<std::size_t>::max();
    std::chrono::steady_clock::duration m_max_duration =
        std::chrono::steady_clock::duration::max();
};

struct PollReport
{
    std::size_t m_records = 0;
    std::chrono::steady_clock::duration m_elapsed{};
    bool m_count_limit_reached = false;
    bool m_time_limit_reached = false;
};

} // namespace lcf::async
