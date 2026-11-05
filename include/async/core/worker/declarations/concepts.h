#pragma once

#include <async/config/macros.h>

#include <concepts>
#include <type_traits>

LCF_ASYNC_EXPORT
namespace lcf::async {

template <typename Progress>
concept worker_progress_c = std::is_nothrow_invocable_r_v<bool, Progress &>;

template <typename Waiter>
concept worker_waiter_c = requires {
        typename Waiter::Observation;
        requires std::copy_constructible<typename Waiter::Observation>;
    } and requires(Waiter & waiter, const Waiter & const_waiter, typename Waiter::Observation observed) {
        { waiter.observe() } noexcept -> std::same_as<typename Waiter::Observation>;
        { waiter.wait(observed) } noexcept -> std::same_as<void>;
        { const_waiter.isStopRequested() } noexcept -> std::same_as<bool>;
        { const_waiter.isClosed() } noexcept -> std::same_as<bool>;
    };

} // namespace lcf::async