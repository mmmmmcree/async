#pragma once

#include <async/config/macros.h>
#include <async/core/notify/declarations/concepts.h>

#include <concepts>
#include <system_error>

LCF_ASYNC_EXPORT
namespace lcf::async {

template <typename Coordinator>
concept worker_coordinator_c = notify::signal_waiter_c<Coordinator>
    and requires(Coordinator & coordinator, const Coordinator & const_coordinator, std::error_code ec) {
        { coordinator.requestStop() } noexcept -> std::same_as<void>;
        { coordinator.reportErrorCode(ec) } noexcept -> std::same_as<void>;
        { const_coordinator.isStopRequested() } noexcept -> std::same_as<bool>;
        { const_coordinator.isFinished() } noexcept -> std::same_as<bool>;
    };

} // namespace lcf::async
