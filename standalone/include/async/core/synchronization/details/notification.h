#pragma once

#include <async/core/protocol.h>
#include <async/core/scheduling/Executor.h>
#include <async/core/synchronization/details/notification_state.h>
#include <async/core/cancellation/details/StopRegistrationGate.h>
#include <optional>
#include <stop_token>

namespace lcf::async::details {

template <typename Receiver>
class NotificationRegistration
{
    using Self = NotificationRegistration;
    struct Cancel
    {
        void operator()() const noexcept
        {
            if (auto registration_sp = m_registration_wp.lock(); registration_sp and registration_sp->m_stop_gate.requestCancellation()) { registration_sp->complete(true); }
        }
        std::weak_ptr<Self> m_registration_wp;
    };
public:
    NotificationRegistration(std::shared_ptr<Notification> notification_sp, Receiver receiver, std::optional<Executor> executor_opt) :
        m_notification_sp(std::move(notification_sp)), m_receiver(std::move(receiver)), m_executor_opt(std::move(executor_opt)),
        m_callback {&Self::invoke, nullptr, this}
    {
        m_waiter.m_complete = &Self::finish;
        m_waiter.m_dispatch = &Self::dispatch;
        m_waiter.m_owner_p = this;
    }
public:
    void start(const std::shared_ptr<Self> & self_sp) noexcept
    {
        try {
            m_stop_callback_opt.emplace(exec::get_stop_token(exec::get_env(m_receiver)), Cancel {self_sp});
            if (m_stop_gate.finishRegistration()) { this->complete(true); return; }
            m_notification_sp->subscribe(std::shared_ptr<NotificationWaiter> {self_sp, &m_waiter});
        } catch (...) { this->complete(false, std::current_exception()); }
    }
private:
    void complete(bool stopped, std::exception_ptr exception = {}) noexcept
    {
        if (m_waiter.m_done.exchange(true, std::memory_order_acq_rel)) { return; }
        m_notification_sp->remove(&m_waiter);
        m_stop_callback_opt.reset();
        if (stopped) { exec::set_stopped(std::move(m_receiver)); }
        else if (exception) { exec::set_error(std::move(m_receiver), std::move(exception)); }
        else { exec::set_value(std::move(m_receiver)); }
    }
    static void finish(NotificationWaiter * waiter_p, std::exception_ptr exception) noexcept
    { static_cast<Self *>(waiter_p->m_owner_p)->complete(false, std::move(exception)); }
    static bool dispatch(std::shared_ptr<NotificationWaiter> waiter_sp) noexcept
    {
        auto * registration_p = static_cast<Self *>(waiter_sp->m_owner_p);
        auto registration_sp = std::shared_ptr<Self> {waiter_sp, registration_p};
        if (not registration_sp->m_executor_opt) { registration_sp->complete(false); return false; }
        registration_sp->m_queued_owner_sp = registration_sp;
        registration_sp->m_executor_opt->enqueue(&registration_sp->m_callback);
        return true;
    }
    static void invoke(Callback * callback_p) noexcept
    {
        auto * registration_p = static_cast<Self *>(callback_p->m_owner_p);
        auto registration_sp = std::move(registration_p->m_queued_owner_sp);
        registration_sp->complete(false);
        registration_sp->m_notification_sp->dispatchNext();
    }
private:
    std::shared_ptr<Notification> m_notification_sp;
    Receiver m_receiver;
    std::optional<Executor> m_executor_opt;
    NotificationWaiter m_waiter;
    Callback m_callback;
    std::optional<std::stop_callback<Cancel>> m_stop_callback_opt;
    StopRegistrationGate m_stop_gate;
    std::shared_ptr<Self> m_queued_owner_sp;
};

struct NotificationSender
{
    using sender_concept = exec::sender_t;
    using completion_signatures = exec::completion_signatures<exec::set_value_t(), exec::set_error_t(std::exception_ptr), exec::set_stopped_t()>;
    template <typename Receiver>
    struct Operation
    {
        using operation_state_concept = exec::operation_state_t;
        void start() noexcept { auto registration_sp = std::move(m_registration_sp); registration_sp->start(registration_sp); }
        std::shared_ptr<NotificationRegistration<Receiver>> m_registration_sp;
    };
    template <exec::receiver Receiver>
    auto connect(Receiver receiver) const
    {
        using Registration = NotificationRegistration<Receiver>;
        return Operation<Receiver> {std::allocate_shared<Registration>(std::pmr::polymorphic_allocator<Registration> {m_resource_p},
            m_notification_sp, std::move(receiver), m_executor_opt)};
    }
    std::shared_ptr<Notification> m_notification_sp;
    std::pmr::memory_resource * m_resource_p;
    std::optional<Executor> m_executor_opt;
};

}
