#pragma once

#include <async/core/driver/declarations/structs.h>
#include <async/core/stream/declarations/concepts.h>
#include <async/core/sink/declarations/concepts.h>

LCF_ASYNC_EXPORT
namespace lcf::async::utils {

template <stream_c Stream, sink_c Sink>
requires std::same_as<typename Stream::value_t, typename Sink::value_t>
    and (not rejecting_sink_c<Sink> or retry_stream_c<Stream>)
DriveReport drive_one(Stream & stream, Sink & sink) noexcept
{
    typename Stream::value_t * record_p = stream.pull();
    if (not record_p) { return {}; }
    if constexpr (rejecting_sink_c<Sink>) {
        const std::error_code ec = sink.receive(*record_p);
        if (ec) {
            stream.requeue(*record_p);
            return {.m_error_code = ec == std::errc::no_buffer_space ? std::error_code {} : ec};
        }
    } else { sink.receive(*record_p); }
    return {.m_has_progress = true};
}

} // namespace lcf::async::utils
