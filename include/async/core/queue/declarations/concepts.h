#pragma once

#include "enums.h"
#include <async/core/declarations/concepts.h>

#include <concepts>
#include <type_traits>

LCF_ASYNC_EXPORT
namespace lcf::async {

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

} // namespace lcf::async
