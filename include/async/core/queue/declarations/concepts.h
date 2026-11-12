#pragma once

#include <async/core/executable/declarations/concepts.h>

#include <concepts>
#include <system_error>
#include <type_traits>

LCF_ASYNC_EXPORT
namespace lcf::async {

template <typename Node>
concept linked_node_c = requires(Node & node, const Node & const_node, Node * next_p) {
    { const_node.getNext() } noexcept -> std::same_as<Node *>;
    { node.setNext(next_p) } noexcept -> std::same_as<void>;
};

template <typename Queue>
concept deque_c = requires(Queue & queue, const Queue & const_queue) {
    typename Queue::Value;
    { queue.tryPopFront() } noexcept -> std::same_as<typename Queue::Value *>;
    { queue.tryPopBack() } noexcept -> std::same_as<typename Queue::Value *>;
    { const_queue.isEmpty() } noexcept -> std::same_as<bool>;
};

template <typename Queue>
concept queue_c = deque_c<Queue> or requires(Queue & queue, const Queue & const_queue) {
    typename Queue::Value;
    { queue.tryPop() } noexcept -> std::same_as<typename Queue::Value *>;
    { const_queue.isEmpty() } noexcept -> std::same_as<bool>;
};

template <typename Queue>
concept queue_producer_c = requires { typename Queue::Value; }
    and requires(Queue & queue, typename Queue::Value & value) {
        { queue.tryEnqueue(value) } noexcept -> std::same_as<std::error_code>;
    };

template <typename Queue>
concept value_queue_c = queue_c<Queue> and queue_producer_c<Queue> and requires {
        typename Queue::Value;
        requires std::same_as<decltype(Queue::k_supports_multiple_producers), const bool>;
        requires std::same_as<decltype(Queue::k_supports_multiple_consumers), const bool>;
        typename std::bool_constant<Queue::k_supports_multiple_producers>;
        typename std::bool_constant<Queue::k_supports_multiple_consumers>;
    } and requires(Queue & queue) {
        { queue.close() } noexcept -> std::same_as<void>;
    };

template <typename Queue>
concept work_queue_c = value_queue_c<Queue> and executable_c<typename Queue::Value>;

template <typename ExecutableInput>
concept executable_input_c = queue_c<ExecutableInput>
    and executable_c<typename ExecutableInput::Value> and requires(ExecutableInput & input) {
        { input.tryPop() } noexcept -> std::same_as<typename ExecutableInput::Value *>;
    };

template <typename Queue>
concept waiting_work_queue_c = work_queue_c<Queue> and requires(Queue & queue) {
    { queue.waitPop() } noexcept -> std::same_as<typename Queue::Value *>;
};

template <typename Queue>
concept stoppable_work_queue_c = waiting_work_queue_c<Queue> and requires(Queue & queue) {
    { queue.requestStop() } noexcept -> std::same_as<void>;
};

} // namespace lcf::async
