#pragma once

#include <async/core/queue/declarations/concepts.h>

#include <concepts>
#include <ranges>

LCF_ASYNC_EXPORT
namespace lcf::async {

template <typename Stream>
concept stream_c = requires(Stream & stream, const Stream & const_stream) {
    typename Stream::value_t;
    { stream.pull() } noexcept -> std::same_as<typename Stream::value_t *>;
    { const_stream.isEmpty() } noexcept -> std::same_as<bool>;
};

template <typename Stream>
concept retry_stream_c = stream_c<Stream> and requires(Stream & stream, typename Stream::value_t & value) {
    { stream.requeue(value) } noexcept -> std::same_as<void>;
};

template <typename Stream>
concept remaining_stream_c = stream_c<Stream> and requires(Stream & stream) {
    { stream.pullRemaining() } noexcept -> std::same_as<typename Stream::value_t *>;
};

template <typename Stream>
concept drainable_stream_c = remaining_stream_c<Stream> and requires(Stream & stream) {
    requires std::ranges::input_range<decltype(stream.drain())>;
    requires std::same_as<std::ranges::range_reference_t<decltype(stream.drain())>, typename Stream::value_t &>;
};

template <typename Source>
concept stream_source_c = stream_c<Source> or consumer_queue_c<Source>;

template <typename Source>
concept drainable_source_c = consumer_queue_c<Source> or drainable_stream_c<Source>;

} // namespace lcf::async
