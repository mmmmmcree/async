#pragma once

#include <async/config/macros.h>
#include <async/core/dispatcher/declarations/structs.h>

#include <concepts>

LCF_ASYNC_EXPORT
namespace lcf::async {

template <typename Dispatcher>
concept request_dispatcher_c = requires(Dispatcher & dispatcher) {
    { dispatcher.dispatchOne() } noexcept -> std::same_as<DispatchReport>;
};

} // namespace lcf::async
