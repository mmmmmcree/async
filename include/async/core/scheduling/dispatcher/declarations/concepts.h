#pragma once

#include <async/core/scheduling/declarations/dispatch_structs.h>

#include <concepts>
#include <system_error>

LCF_ASYNC_EXPORT
namespace lcf::async {

template <typename Source>
concept dispatch_source_c = requires(Source & source, const Source & const_source) {
    { source.tryPop() } noexcept -> std::same_as<DispatchRequest *>;
    { const_source.isEmpty() } noexcept -> std::same_as<bool>;
};

template <typename Router>
concept dispatch_router_c = requires(Router & router, DispatchRequest & request) {
    { router.route(request) } noexcept -> std::same_as<std::error_code>;
};

} // namespace lcf::async
