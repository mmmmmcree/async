#pragma once

#include <async/core/driver/declarations/concepts.h>
#include <async/core/error.h>
#include <async/core/stream/declarations/concepts.h>
#include <async/core/stream/utils/pull_one.h>
#include <async/core/sink/declarations/concepts.h>

LCF_ASYNC_EXPORT
namespace lcf::async::utils {

template <stream_c Stream, sink_c Sink>
requires matched_c<Stream, Sink> and (not rejecting_sink_c<Sink> or retry_stream_c<Stream>)
std::error_code drive_one(Stream & stream, Sink & sink) noexcept
{
    typename Stream::value_t * record_p = utils::pull_one(stream);
    if (not record_p) { return errc::no_work; }
    if constexpr (rejecting_sink_c<Sink>) {
        const auto ec = sink.receive(*record_p);
        if (ec) { stream.requeue(*record_p); }
        return ec;
    } else { sink.receive(*record_p); }
    return {};
}

} // namespace lcf::async::utils
