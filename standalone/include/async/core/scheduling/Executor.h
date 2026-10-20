#pragma once

#include <async/core/scheduling/Scheduler.h>
#include <async/core/scheduling/details/CallbackExecutor.h>

namespace lcf::async {

LCF_ASYNC_EXPORT
class Executor
{
    using Self = Executor;
public:
    struct ScheduleSender
    {
        using sender_concept = exec::sender_t;
        using completion_signatures = exec::completion_signatures<exec::set_value_t(), exec::set_stopped_t()>;
        struct Environment
        {
            details::CallbackExecutor m_callbacks;
            Executor query(exec::get_completion_scheduler_t<exec::set_value_t>) const noexcept { return Executor {m_callbacks}; }
        };
        Environment get_env() const noexcept { return {m_callbacks}; }
        template <exec::receiver Receiver>
        auto connect(Receiver receiver) const { return details::ScheduledOperation<Receiver, Executor> {Executor {m_callbacks}, std::move(receiver)}; }
        details::CallbackExecutor m_callbacks;
    };
public:
    Executor() noexcept = default;
    template <typename Target>
        requires (not std::same_as<Target, Self>) and details::callback_target_c<Target>
    Executor(Target target) : m_callbacks(std::move(target)) {}
    bool operator==(const Self &) const noexcept = default;
    explicit operator bool() const noexcept { return static_cast<bool>(m_callbacks); }
public:
    ScheduleSender schedule() const noexcept
    {
        assert(m_callbacks and "a default-constructed Executor has no target; use Context::getExecutor()");
        return {m_callbacks};
    }
    void enqueue(details::Callback * callback_p) const noexcept { m_callbacks.enqueue(callback_p); }
    void workStarted() const noexcept { m_callbacks.workStarted(); }
    void workFinished() const noexcept { m_callbacks.workFinished(); }
    template <typename Function>
    void post(Function function) const { details::post_callback(*this, std::move(function)); }
private:
    explicit Executor(details::CallbackExecutor callbacks) noexcept : m_callbacks(std::move(callbacks)) {}
private:
    details::CallbackExecutor m_callbacks;
};

}
