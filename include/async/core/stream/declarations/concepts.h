#pragma once

#include <async/core/queue/declarations/concepts.h>

#include <concepts>
#include <ranges>
#include <type_traits>

LCF_ASYNC_EXPORT
namespace lcf::async {

template <typename Source>
using source_object_t = std::remove_pointer_t<std::remove_reference_t<Source>>;

template <typename Source>
using source_value_t = typename source_object_t<Source>::value_t;

template <typename Stream>
concept stream_c = requires(source_object_t<Stream> & stream, const source_object_t<Stream> & const_stream) {
    typename source_value_t<Stream>;
    { stream.pullOne() } noexcept -> std::same_as<source_value_t<Stream> *>;
    { const_stream.isEmpty() } noexcept -> std::same_as<bool>;
};

template <typename Stream>
concept retry_stream_c = stream_c<Stream> and requires(source_object_t<Stream> & stream, source_value_t<Stream> & value) {
    { stream.requeue(value) } noexcept -> std::same_as<void>;
};

// Cleanup bypasses selection policies so every remaining source can be visited.
template <typename Stream>
concept drainable_stream_c = stream_c<Stream> and requires(source_object_t<Stream> & stream) {
    { stream.pullAny() } noexcept -> std::same_as<source_value_t<Stream> *>;
};

template <typename Source>
concept stream_source_c = stream_c<Source> or consumer_queue_c<source_object_t<Source>>;

template <typename Source>
concept drainable_source_c = consumer_queue_c<source_object_t<Source>> or drainable_stream_c<Source>;

template <typename Sources>
concept stream_source_range_c = std::ranges::input_range<Sources>
    and (std::is_lvalue_reference_v<std::ranges::range_reference_t<Sources>>
        or std::is_pointer_v<std::remove_reference_t<std::ranges::range_reference_t<Sources>>>)
    and stream_source_c<std::ranges::range_reference_t<Sources>>;

template <typename Sources>
concept drainable_source_range_c = std::ranges::input_range<Sources>
    and (std::is_lvalue_reference_v<std::ranges::range_reference_t<Sources>>
        or std::is_pointer_v<std::remove_reference_t<std::ranges::range_reference_t<Sources>>>)
    and drainable_source_c<std::ranges::range_reference_t<Sources>>;

} // namespace lcf::async
