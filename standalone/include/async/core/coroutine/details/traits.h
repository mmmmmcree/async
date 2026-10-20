#pragma once

#include <async/core/protocol.h>

namespace lcf::async::details {

template <typename T> struct AwaitablePromise;
template <typename T, typename Receiver> class AwaitableOperation;
template <typename T> class TaskAwaiter;
template <typename T> struct awaitable_signatures
{ using type = exec::completion_signatures<exec::set_value_t(T), exec::set_error_t(std::exception_ptr), exec::set_stopped_t()>; };
template <> struct awaitable_signatures<void>
{ using type = exec::completion_signatures<exec::set_value_t(), exec::set_error_t(std::exception_ptr), exec::set_stopped_t()>; };
template <typename T> inline constexpr bool is_awaitable_v = false;

}
