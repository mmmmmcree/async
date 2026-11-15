#pragma once

#include <async/core/declarations/concepts.h>
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

template <typename Dispatcher>
concept request_dispatcher_c = requires(Dispatcher & dispatcher, const Dispatcher & const_dispatcher) {
    { dispatcher.dispatchOne() } noexcept -> std::same_as<DispatchReport>;
    { const_dispatcher.isEmpty() } noexcept -> std::same_as<bool>;
};

} // namespace lcf::async
