#pragma once

#include <async/core/queue/declarations/concepts.h>

#include <cstddef>
#include <span>

LCF_ASYNC_EXPORT
namespace lcf::async {

template <typename Source>
class RoundRobinVictimSelector
{
public:
    using Value = typename Source::Value;
    RoundRobinVictimSelector(std::span<Source> sources, std::size_t owner_index) noexcept
    requires stealable_work_source_c<Source> :
        m_sources(sources), m_owner_index(owner_index), m_next_index(owner_index + 1) {}
    Value * trySteal() noexcept
    {
        for (std::size_t attempt = 0; attempt < m_sources.size(); ++attempt) {
            const auto index = m_next_index++ % m_sources.size();
            if (index == m_owner_index) { continue; }
            if (auto * value_p = m_sources[index].trySteal()) { return value_p; }
        }
        return nullptr;
    }
private:
    std::span<Source> m_sources;
    std::size_t m_owner_index;
    std::size_t m_next_index;
};

} // namespace lcf::async
