#pragma once

#include <async/core/coroutine/Awaitable.h>
#include <async/core/protocol/details/utility.h>

namespace lcf::async::details {

template <typename Source, typename Receiver>
class StartedOperation
{
    using Self = StartedOperation;
    using Environment = exec::details::EnvironmentOverride<exec::env_of_t<Receiver>, exec::get_scheduler_t, Scheduler>;
    struct ForwardReceiver
    {
        using receiver_concept = exec::receiver_t;
        Environment get_env() const noexcept { return m_owner_p->getEnvironment(); }
        template <typename... Values>
        void set_value(Values &&... values) noexcept { exec::set_value(std::move(m_owner_p->m_receiver), std::forward<Values>(values)...); }
        template <typename Error>
        void set_error(Error && error) noexcept { exec::set_error(std::move(m_owner_p->m_receiver), std::forward<Error>(error)); }
        void set_stopped() noexcept { exec::set_stopped(std::move(m_owner_p->m_receiver)); }
        Self * m_owner_p;
    };
    struct StartReceiver
    {
        using receiver_concept = exec::receiver_t;
        Environment get_env() const noexcept { return m_owner_p->getEnvironment(); }
        void set_value() noexcept { exec::start(m_owner_p->m_source); }
        void set_stopped() noexcept { exec::set_stopped(std::move(m_owner_p->m_receiver)); }
        Self * m_owner_p;
    };
    using SourceOperation = exec::connect_result_t<Source, ForwardReceiver>;
public:
    using operation_state_concept = exec::operation_state_t;
    StartedOperation(Scheduler scheduler, Source source, Receiver receiver) :
        m_scheduler(std::move(scheduler)), m_receiver(std::move(receiver)),
        m_source(exec::connect(std::move(source), ForwardReceiver {this})), m_start(m_scheduler, StartReceiver {this}) {}
    StartedOperation(const Self &) = delete;
    Self & operator=(const Self &) = delete;
public:
    Environment getEnvironment() const noexcept { return {exec::get_env(m_receiver), m_scheduler}; }
    void start() noexcept { m_start.start(); }
private:
    Scheduler m_scheduler;
    Receiver m_receiver;
    SourceOperation m_source;
    ScheduledOperation<StartReceiver, Scheduler> m_start;
};

template <typename Source>
struct StartedSender
{
    using sender_concept = exec::sender_t;
    using completion_signatures = exec::details::concatenate_t<exec::completion_signatures_of_t<Source>, exec::completion_signatures<exec::set_stopped_t()>>;
    template <exec::receiver Receiver>
    auto connect(Receiver receiver) && { return StartedOperation<Source, Receiver> {m_scheduler, std::move(m_source), std::move(receiver)}; }
    Scheduler m_scheduler;
    Source m_source;
};

}

namespace lcf::async {

LCF_ASYNC_EXPORT
inline Awaitable<void> yield() { co_await details::YieldRequest {}; }

LCF_ASYNC_EXPORT
inline details::RescheduleRequest reschedule_on(Executor executor) noexcept { return {std::move(executor)}; }

LCF_ASYNC_EXPORT
template <exec::sender Source>
auto as_sender(Scheduler scheduler, Source work) { return details::StartedSender<Source> {std::move(scheduler), std::move(work)}; }

}
