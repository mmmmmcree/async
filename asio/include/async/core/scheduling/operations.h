#pragma once

#include <async/config.h>

#include <async/core/coroutine/Awaitable.h>
#include <async/core/scheduling/Executor.h>
#include <asio/post.hpp>
#include <asio/use_awaitable.hpp>
#include <utility>

namespace lcf::async {

LCF_ASYNC_EXPORT
template <typename Function>
void post(Executor executor, Function function) { asio::post(std::move(executor), std::move(function)); }

LCF_ASYNC_EXPORT
inline Awaitable<void> yield() { co_await asio::post(asio::use_awaitable); }

}
