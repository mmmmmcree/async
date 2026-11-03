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

template <typename Node>
concept schedule_node_c = requires(Node & node) {
    { node.execute() } noexcept -> std::same_as<void>;
};

template <typename Record>
concept schedule_record_c = requires(Record & record) {
    { record.getScheduleNode() } noexcept;
    requires std::is_lvalue_reference_v<decltype(record.getScheduleNode())>;
    requires schedule_node_c<std::remove_reference_t<decltype(record.getScheduleNode())>>;
};

template <typename Executor, typename Record>
concept executor_c = schedule_record_c<Record> and
    requires(Executor & executor, Record & record) {
        { executor.tryEnqueue(record) } noexcept -> std::same_as<EnqueueResult>;
    };

template <typename Executor>
concept work_executor_c = requires(Executor & executor, const Executor & const_executor) {
    { executor.pollOne() } noexcept -> std::same_as<bool>;
    { executor.close() } noexcept -> std::same_as<void>;
    { const_executor.empty() } noexcept -> std::same_as<bool>;
};

template <typename Executor>
concept blocking_work_executor_c = work_executor_c<Executor> and
    requires(Executor & executor) {
        { executor.waitOne() } noexcept -> std::same_as<bool>;
    };

template <typename Executor>
concept multi_worker_executor_c = blocking_work_executor_c<Executor> and
    requires {
        requires Executor::supports_multiple_workers;
    };

template <typename Executor>
concept stoppable_work_executor_c = blocking_work_executor_c<Executor> and
    requires(Executor & executor) {
        { executor.requestStop() } noexcept -> std::same_as<void>;
    };

template <typename Factory, typename ControlExecutor, typename CpuExecutor>
concept work_executor_factory_c = work_executor_c<ControlExecutor> and
    work_executor_c<CpuExecutor> and
    requires(Factory & factory) {
        { factory.makeControlExecutor() } -> std::same_as<ControlExecutor>;
        { factory.makeCpuExecutor() } -> std::same_as<CpuExecutor>;
    };

template <typename Queue>
concept schedule_queue_c = requires { typename Queue::Node; } and
    schedule_node_c<typename Queue::Node> and
    requires(Queue & queue, const Queue & const_queue, typename Queue::Node & node) {
        { queue.tryEnqueue(node) } noexcept -> std::same_as<EnqueueResult>;
        { queue.tryPop() } noexcept -> std::same_as<typename Queue::Node *>;
        { queue.close() } noexcept -> std::same_as<void>;
        { const_queue.empty() } noexcept -> std::same_as<bool>;
    };

template <typename Queue>
concept blocking_schedule_queue_c = schedule_queue_c<Queue> and
    requires(Queue & queue) {
        { queue.waitPop() } noexcept -> std::same_as<typename Queue::Node *>;
    };

template <typename Queue>
concept stoppable_schedule_queue_c = blocking_schedule_queue_c<Queue> and
    requires(Queue & queue) {
        { queue.requestStop() } noexcept -> std::same_as<void>;
    };

template <typename Thread>
concept thread_c = std::destructible<Thread> and
    requires(Thread & thread, const Thread & const_thread) {
        { const_thread.joinable() } noexcept -> std::same_as<bool>;
        { thread.join() } -> std::same_as<void>;
    };

template <typename Factory, typename Thread>
concept thread_factory_c = thread_c<Thread> and
    requires(Factory & factory, ThreadEntry entry_f, void * state_p) {
        { factory(entry_f, state_p) } -> std::same_as<Thread>;
    };

} // namespace lcf::async
