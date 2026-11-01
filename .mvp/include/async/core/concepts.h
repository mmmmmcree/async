#pragma once

#include "enums.h"

#include <concepts>
#include <cstddef>

namespace lcf::async {

template <typename Record>
concept schedule_record_c = requires(Record & record) {
    { record.getScheduleNode() } noexcept;
};

template <typename Executor, typename Record>
concept executor_c = schedule_record_c<Record>
    && requires(Executor & executor, Record & record) {
        { executor.tryEnqueue(record) } noexcept
            -> std::same_as<EnqueueResult>;
    };

template <typename EventLoop>
concept event_loop_c = requires(EventLoop & loop, const EventLoop & const_loop) {
    { loop.pollOne() } noexcept -> std::same_as<bool>;
    { loop.close() } noexcept -> std::same_as<void>;
    { const_loop.empty() } noexcept -> std::same_as<bool>;
};

template <typename EventLoop>
concept blocking_event_loop_c = event_loop_c<EventLoop> && requires(EventLoop & loop) {
    { loop.waitOne() } noexcept -> std::same_as<bool>;
};

template <typename EventLoop>
concept stoppable_event_loop_c = blocking_event_loop_c<EventLoop>
    && requires(EventLoop & loop) {
        { loop.requestStop() } noexcept -> std::same_as<void>;
    };

template <typename Factory, typename EventLoop>
concept event_loop_factory_c = event_loop_c<EventLoop>
    && requires(Factory & factory, std::size_t index) {
        { factory.makeControlLoop() } -> std::same_as<EventLoop>;
        { factory.makeWorkerLoop(index) } -> std::same_as<EventLoop>;
    };

template <typename Queue, typename Node>
concept schedule_queue_c = requires(Queue & queue, const Queue & const_queue, Node & node) {
    { queue.tryEnqueue(node) } noexcept -> std::same_as<EnqueueResult>;
    { queue.tryPop() } noexcept -> std::same_as<Node *>;
    { queue.close() } noexcept -> std::same_as<void>;
    { const_queue.empty() } noexcept -> std::same_as<bool>;
};

template <typename Queue, typename Node>
concept blocking_schedule_queue_c = schedule_queue_c<Queue, Node> && requires(Queue & queue) {
    { queue.waitPop() } noexcept -> std::same_as<Node *>;
};

template <typename Queue, typename Node>
concept stoppable_schedule_queue_c = blocking_schedule_queue_c<Queue, Node> && requires(Queue & queue) {
    { queue.requestStop() } noexcept -> std::same_as<void>;
};

using ThreadEntry = void (*)(void *) noexcept;

template <typename Thread>
concept thread_c = std::destructible<Thread>
    && requires(Thread & thread, const Thread & const_thread) {
        { const_thread.joinable() } noexcept -> std::same_as<bool>;
        { thread.join() } -> std::same_as<void>;
    };

template <typename Factory, typename Thread>
concept thread_factory_c = thread_c<Thread>
    && requires(Factory & factory, ThreadEntry entry_f, void * state_p) {
        { factory(entry_f, state_p) } -> std::same_as<Thread>;
    };

} // namespace lcf::async
