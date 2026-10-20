#pragma once

#include <async/core/coroutine/details/traits.h>
#include <coroutine>

namespace lcf::async {

LCF_ASYNC_EXPORT
template <typename T = void>
class [[nodiscard]] Awaitable
{
    template <typename, typename> friend class details::AwaitableOperation;
    using Self = Awaitable;
    using Handle = std::coroutine_handle<details::AwaitablePromise<T>>;
public:
    using promise_type = details::AwaitablePromise<T>;
    using sender_concept = exec::sender_t;
    using completion_signatures = typename details::awaitable_signatures<T>::type;
public:
    ~Awaitable() noexcept { if (m_handle) { m_handle.destroy(); } }
    explicit Awaitable(Handle handle) noexcept : m_handle(handle) {}
    Awaitable(const Self &) = delete;
    Awaitable(Self && other) noexcept : m_handle(std::exchange(other.m_handle, {})) {}
    Self & operator=(const Self &) = delete;
    Self & operator=(Self &&) = delete;
public:
    auto operator co_await() && noexcept;
    template <exec::receiver Receiver>
    auto connect(Receiver receiver) &&;
private:
    Handle m_handle;
};

namespace details {
template <typename T> inline constexpr bool is_awaitable_v<Awaitable<T>> = true;
}

}

#include <async/core/coroutine/details/awaitable.h>
