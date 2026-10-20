#pragma once

#include <async/config.h>

#include <asio/awaitable.hpp>

namespace lcf::async {

LCF_ASYNC_EXPORT
template <typename T = void>
using Awaitable = asio::awaitable<T>;

}
