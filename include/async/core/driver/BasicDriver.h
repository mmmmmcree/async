#pragma once

#include <async/core/driver/declarations/concepts.h>
#include <async/core/driver/utils/drive_one.h>
#include <async/core/sink/declarations/concepts.h>

#include <memory>
#include <type_traits>
#include <utility>

LCF_ASYNC_EXPORT
namespace lcf::async {

template <stream_c Stream, sink_c Sink>
requires matched_c<Stream, Sink> and (not rejecting_sink_c<Sink> or retry_stream_c<Stream>)
class BasicDriver
{
public:
    ~BasicDriver() noexcept = default;
    BasicDriver() = delete;
    BasicDriver(Stream & stream, Sink sink) noexcept(std::is_nothrow_move_constructible_v<Sink>) :
        m_stream_p(std::addressof(stream)), m_sink(std::move(sink)) {}
    BasicDriver(const BasicDriver &) = default;
    BasicDriver(BasicDriver &&) = default;
    BasicDriver & operator=(const BasicDriver &) = default;
    BasicDriver & operator=(BasicDriver &&) = default;
public:
    std::error_code driveOne() noexcept { return utils::drive_one(*m_stream_p, m_sink); }
private:
    Stream * m_stream_p;
    Sink m_sink;
};

} // namespace lcf::async
