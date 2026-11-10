#pragma once

#include <async/core/declarations/concepts.h>
#include <async/core/queue/declarations/concepts.h>
#include <async/core/scheduling/dispatcher/declarations/structs.h>

#include <concepts>
#include <cstdint>
#include <system_error>

LCF_ASYNC_EXPORT
namespace lcf::async {

template <typename Record>
concept dispatch_record_c = work_record_c<Record> and requires(
    Record & record, const Record & const_record, std::uint32_t target_index) {
    { const_record.getTargetIndex() } noexcept -> std::same_as<std::uint32_t>;
    { record.setTargetIndex(target_index) } noexcept -> std::same_as<void>;
};

template <typename RequestInput>
concept dispatch_request_input_c = queue_consumer_c<RequestInput>
    and dispatch_record_c<typename RequestInput::Value>
    and linked_node_c<typename RequestInput::Value>;

template <typename Router, typename DispatchRequest>
concept dispatch_router_c = dispatch_record_c<DispatchRequest> and requires(Router & router, DispatchRequest & request) {
    { router.tryRoute(request) } noexcept -> std::same_as<std::error_code>;
};

template <typename Dispatcher>
concept request_dispatcher_c = requires(Dispatcher & dispatcher, const Dispatcher & const_dispatcher) {
    { dispatcher.dispatchOne() } noexcept -> std::same_as<DispatchReport>;
    { const_dispatcher.isEmpty() } noexcept -> std::same_as<bool>;
};

} // namespace lcf::async
