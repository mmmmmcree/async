#pragma once

#include <async/core/queue/declarations/concepts.h>

#include <ranges>
#include <system_error>
#include <type_traits>

LCF_ASYNC_EXPORT
namespace lcf::async::queue::utils {

template <producer_queue_c Queue>
std::error_code enqueue_one(Queue & queue, typename Queue::value_t & value) noexcept
{
    return queue.enqueue(value);
}

template <producer_queue_c Queue>
std::error_code enqueue_one(Queue * queue_p, typename Queue::value_t & value) noexcept { return enqueue_one(*queue_p, value); }

template <producer_queue_range_c Queues>
std::error_code enqueue_one(Queues && queues,
    typename std::remove_pointer_t<std::ranges::range_value_t<Queues>>::value_t & value) noexcept
{
    for (auto && queue : queues) {
        const auto ec = enqueue_one(queue, value);
        if (ec != std::errc::no_buffer_space) { return ec; }
    }
    return std::make_error_code(std::errc::no_buffer_space);
}

} // namespace lcf::async::queue::utils
