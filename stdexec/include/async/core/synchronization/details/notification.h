#pragma once

#include <async/stdexec.h>
#include <async/core/synchronization/details/notification_state.h>
#include <async/core/scheduling/Executor.h>
#include <async/core/cancellation/details/StopRegistrationGate.h>
#include <atomic>
#include <exception>
#include <memory>
#include <memory_resource>
#include <mutex>
#include <optional>
#include <system_error>

namespace lcf::async::details {

template <typename Receiver>
class NotificationRegistration final : public NotificationWaiter, public ::lcf::async::details::Callback,
    public std::enable_shared_from_this<NotificationRegistration<Receiver>>
{
    using Self = NotificationRegistration;
    using StopToken = stde::stop_token_of_t<stde::env_of_t<Receiver>>;
    struct Cancel
    {
        std::weak_ptr<Self> m_registration_wp;
        void operator()() const noexcept
        {
            if (auto registration_sp = m_registration_wp.lock(); registration_sp and registration_sp->m_stop_gate.requestCancellation()) {
                registration_sp->complete(registration_sp->m_cancel_error);
            }
        }
    };
    using StopCallback = stde::stop_callback_for_t<StopToken, Cancel>;
public:
    NotificationRegistration(std::shared_ptr<Notification> notification_sp, Receiver receiver,
        std::optional<::lcf::async::Executor> executor_opt) :
        NotificationWaiter {&Self::finish, &Self::dispatch},
        ::lcf::async::details::Callback {&Self::invoke},
        m_notification_sp(std::move(notification_sp)), m_receiver(std::move(receiver)),
        m_executor_opt(std::move(executor_opt)),
        m_cancel_error(std::make_exception_ptr(std::system_error(std::make_error_code(std::errc::operation_canceled)))) {}
    void start() noexcept
    {
        auto self_sp = this->shared_from_this();
        try {
            m_stop_callback.emplace(stde::get_stop_token(stde::get_env(m_receiver)), Cancel {self_sp});
            if (m_stop_gate.finishRegistration()) { this->complete(m_cancel_error); return; }
            m_notification_sp->subscribe(self_sp);
        } catch (...) {
            this->complete(std::current_exception());
        }
    }
    void complete(std::exception_ptr exception = {}) noexcept
    {
        if (this->m_done.exchange(true, std::memory_order_acq_rel)) { return; }
        m_notification_sp->remove(this);
        m_stop_callback.reset();
        if (exception == m_cancel_error) { stde::set_stopped(std::move(m_receiver)); }
        else if (exception) { stde::set_error(std::move(m_receiver), std::move(exception)); }
        else { stde::set_value(std::move(m_receiver)); }
    }
private:
    static void finish(NotificationWaiter * waiter_p, std::exception_ptr exception) noexcept
    {
        static_cast<Self *>(waiter_p)->complete(std::move(exception));
    }
    static bool dispatch(std::shared_ptr<NotificationWaiter> waiter_sp) noexcept
    {
        auto self_sp = std::static_pointer_cast<Self>(std::move(waiter_sp));
        if (not self_sp->m_executor_opt) { self_sp->complete(); return false; }
        auto * self_p = self_sp.get();
        self_p->m_queued_owner_sp = std::move(self_sp);
        self_p->m_executor_opt->enqueue(self_p);
        return true;
    }
    static void invoke(::lcf::async::details::Callback * callback_p) noexcept
    {
        auto * self_p = static_cast<Self *>(callback_p);
        auto self_sp = std::move(self_p->m_queued_owner_sp);
        self_sp->complete();
        self_sp->m_notification_sp->dispatchNext();
    }
private:
    std::shared_ptr<Notification> m_notification_sp;
    Receiver m_receiver;
    std::optional<::lcf::async::Executor> m_executor_opt;
    std::exception_ptr m_cancel_error;
    std::shared_ptr<Self> m_queued_owner_sp;
    std::optional<StopCallback> m_stop_callback;
    StopRegistrationGate m_stop_gate;
};

struct NotificationSender
{
    using sender_concept = stde::sender_t;
    using completion_signatures = stde::completion_signatures<stde::set_value_t(), stde::set_error_t(std::exception_ptr), stde::set_stopped_t()>;
    template <typename Receiver>
    struct Operation
    {
        using operation_state_concept = stde::operation_state_t;
        void start() noexcept { auto registration_sp = std::move(m_registration_sp); registration_sp->start(); }
        std::shared_ptr<NotificationRegistration<Receiver>> m_registration_sp;
    };
    template <stde::receiver Receiver>
    auto connect(Receiver receiver) const
    {
        using Registration = NotificationRegistration<Receiver>;
        return Operation<Receiver> {std::allocate_shared<Registration>(std::pmr::polymorphic_allocator<Registration> {m_resource_p},
            m_notification_sp, std::move(receiver), m_executor_opt)};
    }
    std::shared_ptr<Notification> m_notification_sp;
    std::pmr::memory_resource * m_resource_p;
    std::optional<::lcf::async::Executor> m_executor_opt {};
};

}
