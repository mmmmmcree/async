#pragma once

#include <async/core/protocol/details/completion.h>
#include <async/core/synchronization/details/CompletionEvent.h>
#include <async/core/cancellation/details/StopRegistrationGate.h>
#include <atomic>

namespace lcf::async::exec::details {

template <typename Signature>
struct shared_signature;
template <typename Tag, typename... Values>
struct shared_signature<Tag(Values...)> { using type = Tag(const std::decay_t<Values> &...); };
template <typename Signatures>
struct shared_signatures;
template <typename... Signatures>
struct shared_signatures<completion_signatures<Signatures...>>
{ using type = concatenate_t<completion_signatures<typename shared_signature<Signatures>::type...>, completion_signatures<set_error_t(const std::exception_ptr &), set_stopped_t()>>; };

template <typename State>
class ConsumerHandle
{
    using Self = ConsumerHandle;
public:
    ~ConsumerHandle() noexcept { this->reset(); }
    explicit ConsumerHandle(std::shared_ptr<State> state_sp) noexcept : m_state_sp(std::move(state_sp)) { m_state_sp->addConsumer(); }
    ConsumerHandle(const Self & other) noexcept : m_state_sp(other.m_state_sp) { if (m_state_sp) { m_state_sp->addConsumer(); } }
    ConsumerHandle(Self &&) noexcept = default;
    Self & operator=(const Self &) = delete;
    Self & operator=(Self &&) = delete;
public:
    State * operator->() const noexcept { return m_state_sp.get(); }
    const std::shared_ptr<State> & getShared() const noexcept { return m_state_sp; }
    void reset() noexcept
    {
        auto state_sp = std::move(m_state_sp);
        if (state_sp) { state_sp->removeConsumer(); }
    }
private:
    std::shared_ptr<State> m_state_sp;
};

template <typename Source>
class SharedState
{
    using Self = SharedState;
    struct SourceReceiver
    {
        using receiver_concept = receiver_t;
        using Environment = EnvironmentOverride<prop<get_allocator_t, std::pmr::polymorphic_allocator<std::byte>>, get_stop_token_t, std::stop_token>;
        Environment get_env() const noexcept
        { return {{get_allocator, std::pmr::polymorphic_allocator<std::byte> {m_state_sp->m_resource_p}}, m_state_sp->m_stop_source.get_token()}; }
        template <typename... Values>
        void set_value(Values &&... values) noexcept
        { auto state_sp = std::move(m_state_sp); state_sp->complete(exec::set_value, std::forward<Values>(values)...); }
        template <typename Error>
        void set_error(Error && error) noexcept
        { auto state_sp = std::move(m_state_sp); state_sp->complete(exec::set_error, std::forward<Error>(error)); }
        void set_stopped() noexcept { auto state_sp = std::move(m_state_sp); state_sp->complete(exec::set_stopped); }
        std::shared_ptr<Self> m_state_sp;
    };
    using SourceOperation = connect_result_t<Source, SourceReceiver>;
public:
    SharedState(Source source, std::pmr::memory_resource * resource_p) : m_source_opt(std::move(source)), m_resource_p(resource_p) {}
public:
    void subscribe(const std::shared_ptr<Self> & state_sp, const lcf::async::details::CompletionEvent::ObserverPtr & observer_sp)
    {
        if (not m_event.subscribe(observer_sp)) { observer_sp->m_notify(); return; }
        if (m_started.exchange(true, std::memory_order_acq_rel)) { return; }
        try {
            m_source.emplace([&] { return exec::connect(std::move(*m_source_opt), SourceReceiver {state_sp}); });
            m_source_opt.reset();
            exec::start(m_source.get());
        } catch (...) { this->complete(set_error, std::current_exception()); }
    }
    void addConsumer() noexcept { m_consumers.fetch_add(1, std::memory_order_relaxed); }
    void removeConsumer() noexcept
    {
        if (m_consumers.fetch_sub(1, std::memory_order_acq_rel) == 1) { m_stop_source.request_stop(); }
    }
    void unsubscribe(const lcf::async::details::CompletionEvent::Observer * observer_p) noexcept { m_event.unsubscribe(observer_p); }
    template <typename Receiver>
    void replay(Receiver receiver) const noexcept { m_completion.replay(std::move(receiver)); }
private:
    template <typename Tag, typename... Values>
    void complete(Tag tag, Values &&... values) noexcept
    {
        auto observers = m_event.complete([&]() noexcept { m_completion.publish(tag, std::forward<Values>(values)...); });
        for (const auto & observer_sp : observers) { observer_sp->m_notify(); }
    }
private:
    std::optional<Source> m_source_opt;
    CompletionStorage<completion_signatures_of_t<Source>> m_completion;
    lcf::async::details::CompletionEvent m_event;
    std::atomic_bool m_started = false;
    std::atomic_size_t m_consumers = 0;
    std::stop_source m_stop_source;
    std::pmr::memory_resource * m_resource_p;
    OperationStorage<SourceOperation> m_source;
};

template <typename Source, typename Receiver>
class SharedSubscription
{
    using Self = SharedSubscription;
    using Observer = lcf::async::details::CompletionEvent::Observer;
    struct Cancel
    {
        void operator()() const noexcept
        {
            if (auto state_sp = m_state_wp.lock(); state_sp and state_sp->m_stop_gate.requestCancellation()) { state_sp->finish(true); }
        }
        std::weak_ptr<Self> m_state_wp;
    };
public:
    SharedSubscription(Receiver receiver, ConsumerHandle<SharedState<Source>> source_rh) :
        m_receiver(std::move(receiver)), m_source_rh(std::move(source_rh)),
        m_observer_sp(std::allocate_shared<Observer>(get_allocator(get_env(m_receiver)))) {}
public:
    void start(const std::shared_ptr<Self> & state_sp) noexcept
    {
        auto source_sp = m_source_rh.getShared();
        try {
            m_observer_sp->m_notify = [state_wp = std::weak_ptr<Self> {state_sp}]() noexcept {
                if (auto state_sp = state_wp.lock()) { state_sp->finish(false); }
            };
            m_stop_callback_opt.emplace(get_stop_token(get_env(m_receiver)), Cancel {state_sp});
            if (m_stop_gate.finishRegistration()) { this->finish(true); return; }
            source_sp->subscribe(source_sp, m_observer_sp);
            if (m_done.load(std::memory_order_acquire)) { source_sp->unsubscribe(m_observer_sp.get()); }
        } catch (...) {
            if (m_done.exchange(true, std::memory_order_acq_rel)) { return; }
            m_stop_callback_opt.reset();
            set_error(std::move(m_receiver), std::current_exception());
        }
    }
private:
    void finish(bool stopped) noexcept
    {
        if (m_done.exchange(true, std::memory_order_acq_rel)) { return; }
        m_stop_callback_opt.reset();
        auto source_rh = std::move(m_source_rh);
        if (stopped) {
            source_rh->unsubscribe(m_observer_sp.get());
            source_rh.reset();
            set_stopped(std::move(m_receiver));
        } else { source_rh->replay(std::move(m_receiver)); }
    }
private:
    Receiver m_receiver;
    ConsumerHandle<SharedState<Source>> m_source_rh;
    std::shared_ptr<Observer> m_observer_sp;
    std::optional<std::stop_callback<Cancel>> m_stop_callback_opt;
    lcf::async::details::StopRegistrationGate m_stop_gate;
    std::atomic_bool m_done = false;
};
template <typename Source>
struct SharedSender
{
    using sender_concept = sender_t;
    using completion_signatures = typename shared_signatures<completion_signatures_of_t<Source>>::type;
    template <typename Receiver>
    struct Operation
    {
        using operation_state_concept = operation_state_t;
        void start() noexcept { auto state_sp = m_state_sp; state_sp->start(state_sp); }
        std::shared_ptr<SharedSubscription<Source, Receiver>> m_state_sp;
    };
    template <receiver Receiver>
    auto connect(Receiver receiver) const
    {
        auto allocator = get_allocator(get_env(receiver));
        return Operation<Receiver> {std::allocate_shared<SharedSubscription<Source, Receiver>>(allocator, std::move(receiver), m_state_rh)};
    }
    ConsumerHandle<SharedState<Source>> m_state_rh;
};

}

namespace lcf::async::exec {

LCF_ASYNC_EXPORT
template <sender Source>
auto split(Source && source, std::pmr::memory_resource * resource_p = std::pmr::get_default_resource())
{
    using State = details::SharedState<std::decay_t<Source>>;
    return details::SharedSender<std::decay_t<Source>> {details::ConsumerHandle<State> {
        std::allocate_shared<State>(std::pmr::polymorphic_allocator<State> {resource_p}, std::forward<Source>(source), resource_p)}};
}

}
