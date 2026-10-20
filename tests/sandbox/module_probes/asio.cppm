module;
#include <asio.hpp>

export module probe.asio;

export namespace probe {
using Context = asio::io_context;
template <typename T = void>
using Awaitable = asio::awaitable<T>;
}
