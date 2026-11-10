#pragma once

#include <async/config/macros.h>

#include <concepts>
#include <cstdint>

LCF_ASYNC_EXPORT
namespace lcf::async {

template <typename Policy>
concept index_selection_policy_c = requires(Policy & policy, std::uint64_t candidate_count) {
    { policy.select(candidate_count) } noexcept -> std::same_as<std::uint64_t>;
};

} // namespace lcf::async
