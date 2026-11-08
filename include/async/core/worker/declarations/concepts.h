#pragma once

#include <async/config/macros.h>
#include <async/core/notify/declarations/concepts.h>

#include <concepts>
#include <type_traits>

LCF_ASYNC_EXPORT
namespace lcf::async {

template <typename Progress>
concept worker_progress_c = std::is_nothrow_invocable_r_v<bool, Progress &>;

template <typename Waiter>
concept worker_waiter_c = notify::signal_waiter_c<Waiter> and requires(const Waiter & const_waiter) {
        { const_waiter.isStopRequested() } noexcept -> std::same_as<bool>;
        { const_waiter.isClosed() } noexcept -> std::same_as<bool>;
    };

} // namespace lcf::async
