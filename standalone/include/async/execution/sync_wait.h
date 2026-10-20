#pragma once

#include <async/core/protocol/details/utility.h>
#include <async/core/scheduling/RunLoop.h>
#include <mutex>
#include <system_error>

namespace lcf::async::exec::details {

template <typename Tuple>
struct WaitState
{
    RunLoop m_loop;
    std::mutex m_mutex;
    std::optional<Tuple> m_values_opt;
    std::exception_ptr m_error;
};
template <typename Tuple>
struct WaitReceiver
{
    using receiver_concept = receiver_t;
    auto get_env() const noexcept
    { return EnvironmentOverride<empty_env, get_scheduler_t, Scheduler> {{}, m_state_p->m_loop.getScheduler()}; }
    template <typename... Values>
    void set_value(Values &&... values) noexcept
    {
        std::lock_guard lock {m_state_p->m_mutex};
        try { m_state_p->m_values_opt.emplace(std::forward<Values>(values)...); }
        catch (...) { m_state_p->m_error = std::current_exception(); }
        m_state_p->m_loop.finish();
    }
    template <typename Error>
    void set_error(Error && error) noexcept
    {
        std::lock_guard lock {m_state_p->m_mutex};
        try {
            if constexpr (std::same_as<std::remove_cvref_t<Error>, std::exception_ptr>) { m_state_p->m_error = error; }
            else if constexpr (std::same_as<std::remove_cvref_t<Error>, std::error_code>) { throw std::system_error(error); }
            else { throw std::forward<Error>(error); }
        } catch (...) { m_state_p->m_error = std::current_exception(); }
        m_state_p->m_loop.finish();
    }
    void set_stopped() noexcept
    {
        std::lock_guard lock {m_state_p->m_mutex};
        m_state_p->m_loop.finish();
    }
    WaitState<Tuple> * m_state_p;
};

}

namespace lcf::async::exec {

LCF_ASYNC_EXPORT
template <sender Sender>
    requires details::value_tuple<completion_signatures_of_t<Sender>>::present
auto sync_wait(Sender && sender) -> std::optional<details::value_tuple_t<Sender>>
{
    using Tuple = details::value_tuple_t<Sender>;
    details::WaitState<Tuple> state;
    auto operation = connect(std::forward<Sender>(sender), details::WaitReceiver<Tuple> {&state});
    start(operation);
    state.m_loop.run();
    std::lock_guard lock {state.m_mutex};
    if (state.m_error) { std::rethrow_exception(state.m_error); }
    return std::move(state.m_values_opt);
}

}
