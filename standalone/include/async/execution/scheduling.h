#pragma once

#include <async/core/protocol/details/completion.h>
#include <async/execution/details/adaptor.h>

namespace lcf::async::exec::details {

template <typename Receiver, typename Signatures, bool Transfer>
class ScheduleState
{
public:
    explicit ScheduleState(Receiver receiver) : m_receiver(std::move(receiver)) {}
public:
    auto getEnvironment() const noexcept { return get_env(m_receiver); }
    template <typename SourceOperation, typename ScheduledOperation>
    void bind(SourceOperation & source, ScheduledOperation & scheduled) noexcept
    { m_source.bind(source); m_schedule.bind(scheduled); }
    void start() noexcept
    {
        if constexpr (Transfer) { m_source.start(); }
        else { m_schedule.start(); }
    }
    template <std::size_t Index, typename Tag, typename... Values>
    void complete(Tag tag, Values &&... values) noexcept
    {
        if constexpr (Index == 0) {
            if constexpr (Transfer) { m_completion.publish(tag, std::forward<Values>(values)...); m_schedule.start(); }
            else { tag(std::move(m_receiver), std::forward<Values>(values)...); }
        } else if constexpr (not std::same_as<Tag, set_value_t>) { tag(std::move(m_receiver), std::forward<Values>(values)...); }
        else if constexpr (Transfer) { m_completion.deliver(std::move(m_receiver)); }
        else { m_source.start(); }
    }
private:
    Receiver m_receiver;
    CompletionStorage<Signatures> m_completion;
    OperationRef m_source;
    OperationRef m_schedule;
};

template <typename Source, typename Scheduler, typename Receiver, bool Transfer>
class ScheduleOperation
{
    using Self = ScheduleOperation;
    using State = ScheduleState<Receiver, completion_signatures_of_t<Source>, Transfer>;
    using Environment = std::conditional_t<Transfer, env_of_t<Receiver>, EnvironmentOverride<env_of_t<Receiver>, get_scheduler_t, Scheduler>>;
    using SourceReceiver = ChildReceiver<State, Environment>;
    using ScheduleReceiver = ChildReceiver<State, env_of_t<Receiver>, 1>;
    using SourceOperation = connect_result_t<Source, SourceReceiver>;
    using Scheduled = decltype(exec::schedule(std::declval<Scheduler>()));
    using ScheduledOperation = connect_result_t<Scheduled, ScheduleReceiver>;
public:
    ScheduleOperation(Source source, Scheduler scheduler, Receiver receiver) : m_state(std::move(receiver))
    {
        const auto environment = [&] {
            if constexpr (Transfer) { return m_state.getEnvironment(); }
            else { return Environment {m_state.getEnvironment(), scheduler}; }
        }();
        m_source.emplace([&] { return exec::connect(std::move(source), SourceReceiver {&m_state, environment}); });
        m_schedule.emplace([&] { return exec::connect(exec::schedule(scheduler), ScheduleReceiver {&m_state, m_state.getEnvironment()}); });
        m_state.bind(m_source.get(), m_schedule.get());
    }
    ScheduleOperation(const Self &) = delete;
    Self & operator=(const Self &) = delete;
public:
    using operation_state_concept = operation_state_t;
    void start() noexcept { m_state.start(); }
private:
    State m_state;
    OperationStorage<SourceOperation> m_source;
    OperationStorage<ScheduledOperation> m_schedule;
};
template <typename Signatures>
struct transferred_signatures;
template <typename... Signatures>
struct transferred_signatures<completion_signatures<Signatures...>>
{
    template <typename Signature>
    struct signature;
    template <typename Tag, typename... Values>
    struct signature<Tag(Values...)> { using type = Tag(std::decay_t<Values>...); };
    using type = concatenate_t<completion_signatures<typename signature<Signatures>::type...>>;
};
template <typename Source, typename Scheduler, bool Transfer>
struct ScheduleSender
{
    using sender_concept = sender_t;
    using SourceSignatures = std::conditional_t<Transfer, typename transferred_signatures<completion_signatures_of_t<Source>>::type, completion_signatures_of_t<Source>>;
    using completion_signatures = concatenate_t<SourceSignatures, exec::completion_signatures<set_error_t(std::exception_ptr), set_stopped_t()>>;
    auto get_env() const noexcept
    {
        if constexpr (Transfer) { return exec::get_env(exec::schedule(m_scheduler)); }
        else { return exec::get_env(m_source); }
    }
    template <receiver Receiver>
    auto connect(Receiver receiver) &&
    { return ScheduleOperation<Source, Scheduler, Receiver, Transfer> {std::move(m_source), std::move(m_scheduler), std::move(receiver)}; }
    Source m_source;
    Scheduler m_scheduler;
};

}

namespace lcf::async::exec {

LCF_ASYNC_EXPORT
template <scheduler Scheduler, sender Source>
auto starts_on(Scheduler scheduler, Source && source)
{ return details::ScheduleSender<std::decay_t<Source>, Scheduler, false> {std::forward<Source>(source), std::move(scheduler)}; }
LCF_ASYNC_EXPORT
template <sender Source, scheduler Scheduler>
auto continues_on(Source && source, Scheduler scheduler)
{ return details::ScheduleSender<std::decay_t<Source>, Scheduler, true> {std::forward<Source>(source), std::move(scheduler)}; }
LCF_ASYNC_EXPORT
template <scheduler Scheduler>
auto continues_on(Scheduler scheduler)
{ return details::Adaptor {[scheduler = std::move(scheduler)]<sender Source>(Source && source) { return continues_on(std::forward<Source>(source), scheduler); }}; }

namespace details {

template <typename Source, typename Scheduler>
struct OnSender
{
    using sender_concept = sender_t;
    using completion_signatures = typename ScheduleSender<Source, Scheduler, true>::completion_signatures;
    template <receiver Receiver>
        requires requires(const Receiver & receiver) { get_scheduler(get_env(receiver)); }
    auto connect(Receiver receiver) &&
    {
        auto original = get_scheduler(get_env(receiver));
        return exec::connect(continues_on(starts_on(std::move(m_scheduler), std::move(m_source)), std::move(original)), std::move(receiver));
    }
    Source m_source;
    Scheduler m_scheduler;
};

}

LCF_ASYNC_EXPORT
template <scheduler Scheduler, sender Source>
auto on(Scheduler scheduler, Source && source)
{ return details::OnSender<std::decay_t<Source>, Scheduler> {std::forward<Source>(source), std::move(scheduler)}; }

}
