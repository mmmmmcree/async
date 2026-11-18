#pragma once

#include <async/config/macros.h>

#include <concepts>
#include <cstddef>
#include <ranges>
#include <type_traits>

LCF_ASYNC_EXPORT
namespace lcf::async {

namespace utils {
using NoSelectionPolicy = std::nullptr_t;
}

template <typename Selection, typename Candidates>
concept candidate_selection_c = std::ranges::input_range<Selection>
    and std::is_lvalue_reference_v<std::ranges::range_reference_t<Selection>>
    and std::same_as<std::ranges::range_reference_t<Selection>, std::ranges::range_reference_t<Candidates>>;

template <typename Policy, typename Candidates>
concept selection_policy_c = std::ranges::forward_range<Candidates>
    and ((std::same_as<std::remove_cv_t<Policy>, utils::NoSelectionPolicy> and candidate_selection_c<Candidates, Candidates>)
        or requires(Policy & policy, Candidates candidates) {
            { policy.select(candidates) } noexcept -> candidate_selection_c<Candidates>;
        });

} // namespace lcf::async
