#pragma once

#include <async/core/protocol.h>
#include <async/core/scheduling/Executor.h>
#include <async/core/timer/details/TimerService.h>
#include <async/core/cancellation/details/StopRegistrationGate.h>
#include <optional>
#include <stdexcept>
#include <stop_token>

namespace lcf::async::details {

template <typename Receiver>
class TimerRegistration
{
    using Self = TimerRegistration;
    struct Cancel
    {
        void operator()() const noexcept
        {
            if (auto registration_sp = m_registration_wp.lock(); registration_sp and registration_sp->m_stop_gate.requestCancellation()) {
                auto entry_sp = std::shared_ptr<TimerEntry> {registration_sp, &registration_sp->m_entry};
                if (not registration_sp->m_timer_service_p->cancel(std::move(entry_sp))) { registration_sp->complete(); }
            }
        }
        std::weak_ptr<Self> m_registration_wp;
    };
public:
    TimerRegistration(Receiver receiver, std::chrono::steady_clock::duration duration, Executor executor) :
        m_receiver(std::move(receiver)), m_executor(std::move(executor)), m_duration(duration) {}
public:
    void start(const std::shared_ptr<Self> & self_sp) noexcept
    {
        const auto * source_p = m_executor.getEventSource();
        m_timer_service_p = source_p ? source_p->target<TimerService>() : nullptr;
        if (not m_timer_service_p) {
            try { throw std::logic_error("delay requires a timer-capable scheduler environment; use schedule_after, as_sender or sync_wait"); }
            catch (...) { exec::set_error(std::move(m_receiver), std::current_exception()); }
            return;
        }
        m_entry.configure(std::chrono::steady_clock::now() + m_duration, Callback::make<&Self::invoke, &Self::abandon>(this));
        m_executor.workStarted();
        m_stop_callback_opt.emplace(exec::get_stop_token(exec::get_env(m_receiver)), Cancel {self_sp});
        auto entry_sp = std::shared_ptr<TimerEntry> {self_sp, &m_entry};
        const auto accepted = m_stop_gate.finishRegistration()
            ? m_timer_service_p->cancel(std::move(entry_sp)) : m_timer_service_p->submit(std::move(entry_sp));
        if (not accepted) { this->complete(); }
    }
private:
    static void invoke(Callback * callback_p) noexcept
    {
        auto * registration_p = static_cast<Self *>(callback_p->m_owner_p);
        auto owner_sp = registration_p->m_entry.releaseReadyOwner();
        registration_p->complete();
    }
    static void abandon(Callback * callback_p) noexcept
    {
        auto * registration_p = static_cast<Self *>(callback_p->m_owner_p);
        auto owner_sp = registration_p->m_entry.releaseReadyOwner();
        registration_p->m_stop_callback_opt.reset();
        registration_p->m_executor.workFinished();
    }
    void complete() noexcept
    {
        m_stop_callback_opt.reset();
        m_executor.workFinished();
        if (auto exception = m_entry.getException()) { exec::set_error(std::move(m_receiver), std::move(exception)); }
        else if (m_entry.isCancelled()) { exec::set_stopped(std::move(m_receiver)); }
        else { exec::set_value(std::move(m_receiver)); }
    }
private:
    Receiver m_receiver;
    Executor m_executor;
    TimerService * m_timer_service_p = nullptr;
    TimerEntry m_entry;
    std::chrono::steady_clock::duration m_duration;
    std::optional<std::stop_callback<Cancel>> m_stop_callback_opt;
    StopRegistrationGate m_stop_gate;
};

}
