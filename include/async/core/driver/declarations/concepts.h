#pragma once

#include <async/config/macros.h>
#include <async/core/sink/declarations/concepts.h>
#include <async/core/stream/declarations/concepts.h>

#include <concepts>
#include <system_error>

LCF_ASYNC_EXPORT
namespace lcf::async {

// A clear code means progress; errc::no_work means the step was idle.
template <typename Driver>
concept driver_c = requires(Driver & driver) {
    { driver.driveOne() } noexcept -> std::same_as<std::error_code>;
};

template <typename Stream, typename Sink>
concept matched_c = stream_c<Stream> and sink_c<Sink>
    and std::same_as<typename Stream::value_t, typename Sink::value_t>;

} // namespace lcf::async
