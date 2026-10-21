#pragma once

#include <async/core/coroutine/details/state.h>
#include <async/core/protocol/details/utility.h>
#include <atomic>
#include <optional>
#include <system_error>

namespace lcf::async::details {

template <typename Error>
std::exception_ptr coroutine_exception(Error && error) noexcept
{
    try {
        if constexpr (std::same_as<std::remove_cvref_t<Error>, std::exception_ptr>) { return std::forward<Error>(error); }
        else if constexpr (std::same_as<std::remove_cvref_t<Error>, std::error_code>) { throw std::system_error(error); }
        else { throw std::forward<Error>(error); }
    } catch (...) { return std::current_exception(); }
}

template <typename Source, typename Promise>
class SenderAwaiter
{
    using Self = SenderAwaiter;
    using Handle = std::coroutine_handle<Promise>;
    using Values = exec::details::value_tuple_t<Source>;
    enum class Phase { eRegistering, eSuspended, eCompleted };
    struct Receiver
    {
        using receiver_concept = exec::receiver_t;
        CoroutineEnvironment get_env() const noexcept { return m_awaiter_p->m_handle.promise().m_state.m_environment; }
        template <typename... Arguments>
        void set_value(Arguments &&... arguments) noexcept
        {
            try { m_awaiter_p->m_values_opt.emplace(std::forward<Arguments>(arguments)...); }
            catch (...) { m_awaiter_p->m_exception = std::current_exception(); }
            m_awaiter_p->publish();
        }
        template <typename Error>
        void set_error(Error && error) noexcept
        {
            m_awaiter_p->m_exception = coroutine_exception(std::forward<Error>(error));
            m_awaiter_p->publish();
        }
        void set_stopped() noexcept { m_awaiter_p->m_stopped = true; m_awaiter_p->publish(); }
        Self * m_awaiter_p;
    };
    using Operation = exec::connect_result_t<Source, Receiver>;
public:
    explicit SenderAwaiter(Source source) : m_source(std::move(source)), m_callback(Callback::make<&Self::resume>(this)) {}
    SenderAwaiter(const Self &) = delete;
    Self & operator=(const Self &) = delete;
public:
    bool await_ready() const noexcept { return false; }
    bool await_suspend(Handle handle) noexcept
    {
        m_handle = handle;
        try {
            m_operation.emplace([&] { return exec::connect(std::move(m_source), Receiver {this}); });
            exec::start(m_operation.get());
        } catch (...) { m_exception = std::current_exception(); return false; }
        if (m_phase.exchange(Phase::eSuspended, std::memory_order_acq_rel) != Phase::eCompleted) { return true; }
        if (m_stopped) { handle.promise().m_state.stop(); return true; }
        return false;
    }
    auto await_resume()
    {
        if (m_exception) { std::rethrow_exception(std::move(m_exception)); }
        if constexpr (std::tuple_size_v<Values> == 0) { return; }
        else if constexpr (std::tuple_size_v<Values> == 1) { return std::get<0>(std::move(*m_values_opt)); }
        else { return std::move(*m_values_opt); }
    }
private:
    void publish() noexcept
    {
        if (m_phase.exchange(Phase::eCompleted, std::memory_order_acq_rel) != Phase::eSuspended) { return; }
        auto executor = m_handle.promise().m_state.m_environment.m_executor;
        if (executor) { executor.enqueue(&m_callback); }
        else { Self::resume(&m_callback); }
    }
    static void resume(Callback * callback_p) noexcept
    {
        auto * awaiter_p = static_cast<Self *>(callback_p->m_owner_p);
        auto handle = awaiter_p->m_handle;
        if (awaiter_p->m_stopped) { handle.promise().m_state.stop(); }
        else { handle.resume(); }
    }
private:
    Source m_source;
    Handle m_handle;
    std::optional<Values> m_values_opt;
    std::exception_ptr m_exception;
    bool m_stopped = false;
    std::atomic<Phase> m_phase {Phase::eRegistering};
    Callback m_callback;
    exec::details::OperationStorage<Operation> m_operation;
};

}
