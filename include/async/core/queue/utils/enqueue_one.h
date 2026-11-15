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
    return queue.tryEnqueue(value);
}

template <std::ranges::input_range Queues>
requires std::is_lvalue_reference_v<std::ranges::range_reference_t<Queues>>
    and producer_queue_c<std::remove_reference_t<std::ranges::range_reference_t<Queues>>>
std::error_code enqueue_one(Queues && queues, typename std::ranges::range_value_t<Queues>::value_t & value) noexcept
{
    for (auto & queue : queues) {
        const std::error_code error_code = enqueue_one(queue, value);
        if (error_code != std::errc::no_buffer_space) { return error_code; }
    }
    return std::make_error_code(std::errc::no_buffer_space);
}

} // namespace lcf::async::queue::utils
