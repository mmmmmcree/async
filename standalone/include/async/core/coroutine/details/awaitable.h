#pragma once

#include <async/core/coroutine/Awaitable.h>
#include <async/core/coroutine/details/ReturnStorage.h>
#include <async/core/coroutine/FrameAllocatorScope.h>
#include <async/core/coroutine/details/sender_awaiter.h>
#include <stdexcept>

namespace lcf::async::details {

template <typename T>
struct AwaitablePromise : ReturnStorage<T>
{
    using Self = AwaitablePromise;
    struct FinalAwaiter
    {
        bool await_ready() const noexcept { return false; }
        std::coroutine_handle<> await_suspend(std::coroutine_handle<Self> handle) const noexcept { return handle.promise().m_state.finish(); }
        void await_resume() const noexcept {}
    };
    static void * operator new(std::size_t size) { return allocate_frame(size, std::max(alignof(Self), ReturnStorage<T>::k_alignment)); }
    static void operator delete(void * frame_p, std::size_t size) noexcept
    { deallocate_frame(frame_p, size, std::max(alignof(Self), ReturnStorage<T>::k_alignment)); }
    Awaitable<T> get_return_object() noexcept { return Awaitable<T> {std::coroutine_handle<Self>::from_promise(*this)}; }
    std::suspend_always initial_suspend() const noexcept { return {}; }
    FinalAwaiter final_suspend() const noexcept { return {}; }
    void unhandled_exception() noexcept { m_exception = std::current_exception(); }
    CoroutineEnvironment get_env() const noexcept { return m_state.m_environment; }
    template <typename Value>
    auto await_transform(Awaitable<Value> task) noexcept { return std::move(task).operator co_await(); }
    template <exec::sender Sender>
        requires (not is_awaitable_v<std::remove_cvref_t<Sender>>)
    auto await_transform(Sender && sender) { return SenderAwaiter<std::remove_cvref_t<Sender>, Self> {std::forward<Sender>(sender)}; }
    auto await_transform(YieldRequest)
    {
        if (not m_state.m_environment.m_executor) { throw std::logic_error("yield requires a scheduler environment; use as_sender or sync_wait"); }
        auto work = exec::schedule(m_state.m_environment.m_executor);
        return SenderAwaiter<decltype(work), Self> {std::move(work)};
    }
    auto await_transform(RescheduleRequest request)
    {
        m_state.m_environment.m_executor = std::move(request.m_executor);
        auto work = exec::schedule(m_state.m_environment.m_executor);
        return SenderAwaiter<decltype(work), Self> {std::move(work)};
    }
    template <typename Value>
        requires (not exec::sender<Value>)
    decltype(auto) await_transform(Value && value) { return std::forward<Value>(value); }
    CoroutineState m_state;
    std::exception_ptr m_exception;
};

template <typename T>
class TaskAwaiter
{
    using Self = TaskAwaiter;
    using Promise = AwaitablePromise<T>;
    using Handle = std::coroutine_handle<Promise>;
public:
    ~TaskAwaiter() noexcept { if (m_handle) { m_handle.destroy(); } }
    explicit TaskAwaiter(Handle handle) noexcept : m_handle(handle) {}
    TaskAwaiter(const Self &) = delete;
    Self & operator=(const Self &) = delete;
public:
    bool await_ready() const noexcept { return false; }
    template <typename Parent>
    Handle await_suspend(std::coroutine_handle<Parent> parent) noexcept
    {
        auto & state = m_handle.promise().m_state;
        state.m_parent_p = &parent.promise().m_state;
        state.m_environment = state.m_parent_p->m_environment;
        state.m_return_executor = state.m_environment.m_executor;
        state.m_continuation = parent;
        return m_handle;
    }
    T await_resume()
    {
        struct FrameOwner
        {
            ~FrameOwner() noexcept { m_handle.destroy(); }
            Handle m_handle;
        } owner {std::exchange(m_handle, {})};
        auto & promise = owner.m_handle.promise();
        if (promise.m_exception) { std::rethrow_exception(std::move(promise.m_exception)); }
        return promise.takeValue();
    }
private:
    Handle m_handle;
};

template <typename T, typename Receiver>
class AwaitableOperation
{
    using Self = AwaitableOperation;
    using Handle = std::coroutine_handle<AwaitablePromise<T>>;
public:
    using operation_state_concept = exec::operation_state_t;
    ~AwaitableOperation() noexcept { if (m_handle) { m_handle.destroy(); } }
    AwaitableOperation(Awaitable<T> work, Receiver receiver) : m_receiver(std::move(receiver))
    { m_handle = std::exchange(work.m_handle, {}); }
    AwaitableOperation(const Self &) = delete;
    Self & operator=(const Self &) = delete;
public:
    void start() noexcept
    {
        try {
            auto environment = exec::get_env(m_receiver);
            auto & state = m_handle.promise().m_state;
            if constexpr (requires { exec::get_scheduler(environment); }) { state.m_environment.m_executor = Executor {exec::get_scheduler(environment)}; }
            state.m_environment.m_stop_token = exec::get_stop_token(environment);
            state.m_environment.m_resource_p = exec::get_allocator(environment).resource();
            state.m_return_executor = state.m_environment.m_executor;
            state.m_owner_p = this;
            state.m_complete = &Self::complete;
            if (state.m_environment.m_stop_token.stop_requested()) { exec::set_stopped(std::move(m_receiver)); return; }
            if (state.m_return_executor) { state.m_return_executor.workStarted(); m_work_started = true; }
            m_handle.resume();
        } catch (...) { exec::set_error(std::move(m_receiver), std::current_exception()); }
    }
private:
    static void complete(void * owner_p) noexcept
    {
        auto & operation = *static_cast<Self *>(owner_p);
        auto & promise = operation.m_handle.promise();
        if (operation.m_work_started) { promise.m_state.m_return_executor.workFinished(); }
        if (promise.m_state.m_stopped) { exec::set_stopped(std::move(operation.m_receiver)); return; }
        if (promise.m_exception) { exec::set_error(std::move(operation.m_receiver), promise.m_exception); return; }
        try {
            if constexpr (std::is_void_v<T>) { exec::set_value(std::move(operation.m_receiver)); }
            else {
                auto value = promise.takeValue();
                exec::set_value(std::move(operation.m_receiver), std::move(value));
            }
        } catch (...) { exec::set_error(std::move(operation.m_receiver), std::current_exception()); }
    }
private:
    Handle m_handle {};
    Receiver m_receiver;
    bool m_work_started = false;
};

}

namespace lcf::async {

template <typename T>
auto Awaitable<T>::operator co_await() && noexcept { return details::TaskAwaiter<T> {std::exchange(m_handle, {})}; }
template <typename T>
template <exec::receiver Receiver>
auto Awaitable<T>::connect(Receiver receiver) &&
{ return details::AwaitableOperation<T, Receiver> {std::move(*this), std::move(receiver)}; }

}
