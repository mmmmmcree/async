#pragma once

#include <async/core/scheduling/declarations/concepts.h>

#include <concepts>

LCF_ASYNC_EXPORT
namespace lcf::async {

// Each attempt is finite. visitNext visits one candidate and returns true, or
// returns false when exhausted. The driver decides how to execute the candidate.
template <typename Selector>
concept executor_selector_c = requires(Selector & selector, const Selector & const_selector) {
    { selector.beginSelection() } noexcept -> std::same_as<void>;
    { selector.visitNext([](work_executor_c auto &) noexcept {}) } noexcept -> std::same_as<bool>;
    { const_selector.isEmpty() } noexcept -> std::same_as<bool>;
};

} // namespace lcf::async
