#pragma once

#include <async/config.h>

#include <async/core/coroutine/Awaitable.h>
#include <asio/steady_timer.hpp>
#include <asio/this_coro.hpp>
#include <asio/use_awaitable.hpp>
#include <chrono>

namespace lcf::async {

LCF_ASYNC_EXPORT
inline Awaitable<void> delay(std::chrono::steady_clock::duration duration)
{
    asio::steady_timer timer {co_await asio::this_coro::executor};
    timer.expires_after(duration);
    co_await timer.async_wait(asio::use_awaitable);
}

}
