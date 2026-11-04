#pragma once

#include <async/core/declarations/concepts.h>
#include <async/core/queue/declarations/enums.h>
#include <async/protocol.h>

#include <concepts>
#include <cstdint>

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

template <typename Notifier>
concept work_notifier_c = requires(Notifier & notifier) {
    { notifier.notifyOne() } noexcept -> std::same_as<void>;
};

template <typename Submitter, typename Record>
concept record_submitter_c = work_record_c<Record> and
    requires(Submitter & submitter, Record & record) {
        { submitter.tryEnqueue(record) } noexcept -> std::same_as<EnqueueResult>;
    };

template <typename Submitter, typename Record>
concept indexed_record_submitter_c = work_record_c<Record> and
    requires(Submitter & submitter, std::uint32_t target_index, Record & record) {
        { submitter.tryEnqueue(target_index, record) } noexcept -> std::same_as<EnqueueResult>;
    };

template <typename Executor>
concept work_executor_c = requires(Executor & executor, const Executor & const_executor) {
    { executor.executeOne() } noexcept -> std::same_as<bool>;
    { executor.close() } noexcept -> std::same_as<void>;
    { const_executor.isEmpty() } noexcept -> std::same_as<bool>;
};

template <typename Executor>
concept waiting_work_executor_c = work_executor_c<Executor> and
    requires(Executor & executor) {
        { executor.waitAndExecuteOne() } noexcept -> std::same_as<bool>;
    };

template <typename Executor>
concept stoppable_work_executor_c = waiting_work_executor_c<Executor> and
    requires(Executor & executor) {
        { executor.requestStop() } noexcept -> std::same_as<void>;
    };

} // namespace lcf::async
