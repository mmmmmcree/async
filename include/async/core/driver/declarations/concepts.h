#pragma once

#include <async/core/driver/declarations/structs.h>
#include <async/core/sink/declarations/concepts.h>
#include <async/core/stream/declarations/concepts.h>

#include <concepts>

LCF_ASYNC_EXPORT
namespace lcf::async {

template <typename Driver>
concept driver_c = requires(Driver & driver) {
    { driver.driveOne() } noexcept -> std::same_as<DriveReport>;
};

template <typename Stream, typename Sink>
concept matched_c = stream_c<Stream> and sink_c<Sink>
    and std::same_as<typename Stream::value_t, typename Sink::value_t>;

} // namespace lcf::async
