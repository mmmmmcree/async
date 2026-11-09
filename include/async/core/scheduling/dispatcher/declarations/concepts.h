#pragma once

#include <async/core/declarations/concepts.h>
#include <async/core/queue/declarations/concepts.h>

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

template <typename Source>
concept dispatch_source_c = requires {
    typename Source::Value;
    requires dispatch_record_c<typename Source::Value>;
    requires linked_node_c<typename Source::Value>;
} and requires(Source & source, const Source & const_source) {
    { source.tryPop() } noexcept -> std::same_as<typename Source::Value *>;
    { const_source.isEmpty() } noexcept -> std::same_as<bool>;
};

template <typename Router, typename DispatchRequest>
concept dispatch_router_c = dispatch_record_c<DispatchRequest> and requires(Router & router, DispatchRequest & request) {
    { router.route(request) } noexcept -> std::same_as<std::error_code>;
};

} // namespace lcf::async
