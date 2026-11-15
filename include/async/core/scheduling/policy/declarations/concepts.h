#pragma once

#include <async/config/macros.h>

#include <concepts>
#include <ranges>
#include <type_traits>

LCF_ASYNC_EXPORT
namespace lcf::async {

template <typename Selection, typename Candidates>
concept candidate_selection_c = std::ranges::input_range<Selection>
    and std::is_lvalue_reference_v<std::ranges::range_reference_t<Selection>>
    and std::same_as<std::ranges::range_reference_t<Selection>, std::ranges::range_reference_t<Candidates>>;

template <typename Policy, typename Candidates>
concept selection_policy_c = std::ranges::forward_range<Candidates> and requires(Policy & policy, Candidates candidates) {
    { policy.select(candidates) } noexcept -> candidate_selection_c<Candidates>;
};

} // namespace lcf::async
