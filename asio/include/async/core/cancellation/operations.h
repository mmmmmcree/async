#pragma once

#include <async/config.h>

#include <asio/bind_cancellation_slot.hpp>
#include <asio/cancellation_signal.hpp>
#include <utility>

namespace lcf::async {

LCF_ASYNC_EXPORT
using CancellationSource = asio::cancellation_signal;

LCF_ASYNC_EXPORT
template <typename Completion>
auto bind_cancellation(CancellationSource & source, Completion completion)
{
    return asio::bind_cancellation_slot(source.slot(), std::move(completion));
}

LCF_ASYNC_EXPORT
inline void request_cancellation(CancellationSource & source) { source.emit(asio::cancellation_type::terminal); }

}
