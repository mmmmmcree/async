#pragma once

#include <async/core/declarations/concepts.h>
#include <async/core/executor/declarations/structs.h>
#include <async/core/scheduling/declarations/enums.h>
#include <async/protocol.h>

#include <concepts>
#include <cstdint>
#include <system_error>

LCF_ASYNC_EXPORT
namespace lcf::async {

template <typename Type>
concept sender_c = protocols::sender<Type>;

template <typename Type>
concept receiver_c = protocols::receiver<Type>;

template <typename Type>
concept scheduler_c = protocols::scheduler<Type>;

template <typename Type>
concept operation_state_c = protocols::operation_state<Type>;

template <typename Receiver, typename Completions>
concept receiver_of_c = protocols::receiver_of<Receiver, Completions>;

template <typename Record>
concept scheduling_record_c = work_record_c<Record> and requires(Record & record,
    const Record & const_record, std::uint32_t index, Priority priority) {
    { const_record.getSchedulerIndex() } noexcept -> std::same_as<std::uint32_t>;
    { const_record.getPriority() } noexcept -> std::same_as<Priority>;
    { record.setSchedulerIndex(index) } noexcept -> std::same_as<void>;
    { record.setPriority(priority) } noexcept -> std::same_as<void>;
};

template <typename Submitter, typename Record>
concept record_submitter_c = scheduling_record_c<Record> and
    requires(Submitter & submitter, Record & record) {
        { submitter.submit(record) } noexcept -> std::same_as<std::error_code>;
    };

template <typename Executor>
concept work_executor_c = requires(Executor & executor, const Executor & const_executor) {
    { executor.executeOne() } noexcept -> std::same_as<ExecutionReport>;
};

template <typename ExecutionPort>
concept execution_port_c = requires(ExecutionPort & port) {
    { port.executeOne() } noexcept -> std::same_as<ExecutionReport>;
};

} // namespace lcf::async
