#pragma once

#include "enums.h"
#include <async/protocol.h>
#include <async/core/thread_types.h>

#include <concepts>
#include <cstddef>
#include <type_traits>

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

template <typename Executable>
concept executable_c = requires(Executable & executable) {
    { executable.execute() } noexcept -> std::same_as<void>;
};

template <typename Record>
concept work_record_c = requires(Record & record) {
    { record.getExecutable() } noexcept;
    requires std::is_lvalue_reference_v<decltype(record.getExecutable())>;
    requires executable_c<std::remove_reference_t<decltype(record.getExecutable())>>;
};

template <typename Submitter, typename Record>
concept record_submitter_c = work_record_c<Record> and
    requires(Submitter & submitter, Record & record) {
        { submitter.tryEnqueue(record) } noexcept -> std::same_as<EnqueueResult>;
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
concept multi_worker_executor_c = waiting_work_executor_c<Executor> and
    requires {
        requires std::same_as<decltype(Executor::k_supports_multiple_workers), const bool>;
        requires Executor::k_supports_multiple_workers;
    };

template <typename Executor>
concept stoppable_work_executor_c = waiting_work_executor_c<Executor> and
    requires(Executor & executor) {
        { executor.requestStop() } noexcept -> std::same_as<void>;
    };

template <typename Factory, typename Executor>
concept context_executor_factory_c = work_executor_c<Executor> and
    requires(Factory & factory) {
        { factory.makeExecutor() } -> std::same_as<Executor>;
    };

template <typename Queue>
concept work_queue_c = requires {
        typename Queue::Executable;
        requires std::same_as<decltype(Queue::k_supports_multiple_producers), const bool>;
        requires std::same_as<decltype(Queue::k_supports_multiple_consumers), const bool>;
        typename std::bool_constant<Queue::k_supports_multiple_producers>;
        typename std::bool_constant<Queue::k_supports_multiple_consumers>;
    } and executable_c<typename Queue::Executable> and
    requires(Queue & queue, const Queue & const_queue, typename Queue::Executable & executable) {
        { queue.tryEnqueue(executable) } noexcept -> std::same_as<EnqueueResult>;
        { queue.tryPop() } noexcept -> std::same_as<typename Queue::Executable *>;
        { queue.close() } noexcept -> std::same_as<void>;
        { const_queue.isEmpty() } noexcept -> std::same_as<bool>;
    };

template <typename Queue>
concept waiting_work_queue_c = work_queue_c<Queue> and
    requires(Queue & queue) {
        { queue.waitPop() } noexcept -> std::same_as<typename Queue::Executable *>;
    };

template <typename Queue>
concept stoppable_work_queue_c = waiting_work_queue_c<Queue> and
    requires(Queue & queue) {
        { queue.requestStop() } noexcept -> std::same_as<void>;
    };

// Join capability alone makes no claim about the kind of execution agent.
template <typename Handle>
concept join_handle_c = std::destructible<Handle> and
    requires(Handle & handle, const Handle & const_handle) {
        { const_handle.joinable() } noexcept -> std::same_as<bool>;
        { handle.join() } -> std::same_as<void>;
    };

// Semantic contract: launch entry exactly once on a separate thread, and return
// its join handle. A failed launch must leave no entry or thread in flight.
template <typename Launcher, typename ThreadHandle>
concept thread_launcher_c = join_handle_c<ThreadHandle> and
    requires(Launcher & launcher, ThreadEntry entry_f, void * state_p) {
        { launcher(entry_f, state_p) } -> std::same_as<ThreadHandle>;
    };

} // namespace lcf::async
