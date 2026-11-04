#pragma once

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
    { driver.executeOne() } noexcept -> std::same_as<bool>;
    { driver.run() } noexcept -> std::same_as<void>;
    { const_driver.getState() } noexcept -> std::same_as<WorkerState>;
};

} // namespace lcf::async
