#pragma once

#include <async/config/macros.h>
#include <concepts>
#include <system_error>

LCF_ASYNC_EXPORT
namespace lcf::async {

template <typename Sink>
concept sink_c = requires(Sink & sink, typename Sink::value_t & value) {
    typename Sink::value_t;
    requires std::same_as<decltype(sink.receive(value)), void>
        or std::same_as<decltype(sink.receive(value)), std::error_code>;
    requires noexcept(sink.receive(value));
};

template <typename Sink>
concept rejecting_sink_c = sink_c<Sink>
    and std::same_as<decltype(std::declval<Sink &>().receive(std::declval<typename Sink::value_t &>())), std::error_code>;

} // namespace lcf::async
