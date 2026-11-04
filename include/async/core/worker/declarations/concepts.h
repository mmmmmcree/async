#pragma once

#include "aliases.h"
#include "enums.h"
#include <async/core/scheduling/declarations/concepts.h>

#include <concepts>

LCF_ASYNC_EXPORT
namespace lcf::async {

template <typename Executor>
concept multi_worker_executor_c = work_executor_c<Executor> and
    requires {
        requires std::same_as<decltype(Executor::k_supports_multiple_workers), const bool>;
        requires Executor::k_supports_multiple_workers;
    };

template <typename Handle>
concept join_handle_c = std::destructible<Handle> and
    requires(Handle & handle, const Handle & const_handle) {
        { const_handle.joinable() } noexcept -> std::same_as<bool>;
        { handle.join() } -> std::same_as<void>;
    };

template <typename Launcher, typename ThreadHandle>
concept thread_launcher_c = join_handle_c<ThreadHandle> and
    requires(Launcher & launcher, ThreadEntry entry_f, void * state_p) {
        { launcher(entry_f, state_p) } -> std::same_as<ThreadHandle>;
    };

template <typename Selector>
concept executor_selector_c = requires {
        typename Selector::Executor;
        requires work_executor_c<typename Selector::Executor>;
    } and requires(Selector & selector, const Selector & const_selector) {
        { selector.beginSelection() } noexcept -> std::same_as<void>;
        { selector.selectNext() } noexcept -> std::same_as<typename Selector::Executor *>;
        { const_selector.isEmpty() } noexcept -> std::same_as<bool>;
    };

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

template <typename Driver>
concept worker_driver_c = requires(Driver & driver, const Driver & const_driver) {
    { driver.run() } noexcept -> std::same_as<void>;
    { const_driver.getState() } noexcept -> std::same_as<WorkerState>;
};

} // namespace lcf::async
