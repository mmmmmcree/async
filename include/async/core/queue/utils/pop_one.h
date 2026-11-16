#pragma once

#include <async/core/queue/utils/drain.h>

#include <ranges>
#include <type_traits>

LCF_ASYNC_EXPORT
namespace lcf::async::queue::utils {

template <DrainDirection direction = from_front, consumer_queue_c Queue>
typename Queue::value_t * pop_one(Queue & queue) noexcept
{
    if constexpr (consumer_deque_c<Queue>) {
        if constexpr (direction == from_front) { return queue.popFront(); }
        else { return queue.popBack(); }
    } else { return queue.pop(); }
}

template <DrainDirection direction = from_front, consumer_queue_c Queue>
typename Queue::value_t * pop_one(Queue * queue_p) noexcept { return pop_one<direction>(*queue_p); }

template <DrainDirection direction = from_front, consumer_queue_range_c Queues>
typename std::remove_pointer_t<std::ranges::range_value_t<Queues>>::value_t * pop_one(Queues && queues) noexcept
{
    for (auto && queue : queues) {
        if (auto * value_p = pop_one<direction>(queue)) { return value_p; }
    }
    return nullptr;
}

} // namespace lcf::async::queue::utils
