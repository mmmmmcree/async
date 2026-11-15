#pragma once

#include <async/core/policy/declarations/concepts.h>

#include <ranges>

LCF_ASYNC_EXPORT
namespace lcf::async::policy {

class StrictPriority
{
public:
    ~StrictPriority() noexcept = default;
    StrictPriority() noexcept = default;
    StrictPriority(const StrictPriority &) noexcept = default;
    StrictPriority(StrictPriority &&) noexcept = default;
    StrictPriority & operator=(const StrictPriority &) noexcept = default;
    StrictPriority & operator=(StrictPriority &&) noexcept = default;
    template <std::ranges::view Candidates>
    requires std::ranges::forward_range<Candidates>
    Candidates select(Candidates candidates) noexcept { return candidates; }
};

} // namespace lcf::async::policy
