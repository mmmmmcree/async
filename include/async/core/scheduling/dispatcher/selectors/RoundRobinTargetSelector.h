#pragma once

#include <async/config/macros.h>

#include <cstddef>

LCF_ASYNC_EXPORT
namespace lcf::async {

class RoundRobinTargetSelector
{
public:
    explicit RoundRobinTargetSelector(std::size_t start_index = 0) noexcept : m_next_index(start_index) {}
    std::size_t select(std::size_t target_count) noexcept
    {
        const auto index = m_next_index % target_count;
        m_next_index = index + 1;
        return index;
    }
private:
    std::size_t m_next_index;
};

} // namespace lcf::async
