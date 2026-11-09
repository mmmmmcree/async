#pragma once

#include <async/config/macros.h>

#include <cstdint>

LCF_ASYNC_EXPORT
namespace lcf::async {

class RoundRobinTargetSelector
{
public:
    explicit RoundRobinTargetSelector(std::uint64_t start_index = 0) noexcept : m_next_index(start_index) {}
    std::uint64_t select(std::uint64_t target_count) noexcept
    {
        const std::uint64_t index = m_next_index % target_count;
        m_next_index = index + 1;
        return index;
    }
private:
    std::uint64_t m_next_index;
};

} // namespace lcf::async
