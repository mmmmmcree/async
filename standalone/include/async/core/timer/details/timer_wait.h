#pragma once

#include <async/core/protocol.h>
#include <async/core/scheduling/Executor.h>
#include <async/core/timer/details/timer.h>
#include <async/core/cancellation/details/StopRegistrationGate.h>
#include <optional>
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
                TimerService::instance().cancel(std::shared_ptr<TimerEntry> {registration_sp, &registration_sp->m_entry});
            }
        }
        std::weak_ptr<Self> m_registration_wp;
    };
public:
    TimerRegistration(Receiver receiver, std::chrono::steady_clock::duration duration, Executor executor) :
        m_receiver(std::move(receiver)), m_executor(std::move(executor)),
        m_entry {{}, &Self::finish, {}, false, this}, m_duration(duration),
        m_callback {&Self::invoke, nullptr, this}
    { TimerService::instance(); }
public:
    void start(const std::shared_ptr<Self> & self_sp) noexcept
    {
        m_entry.m_deadline = std::chrono::steady_clock::now() + m_duration;
        if (m_executor) { m_executor.workStarted(); }
        m_stop_callback_opt.emplace(exec::get_stop_token(exec::get_env(m_receiver)), Cancel {self_sp});
        auto entry_sp = std::shared_ptr<TimerEntry> {self_sp, &m_entry};
        if (m_stop_gate.finishRegistration()) { TimerService::instance().cancel(std::move(entry_sp)); return; }
        TimerService::instance().submit(std::move(entry_sp));
    }
private:
    static void finish(std::shared_ptr<TimerEntry> entry_sp, bool cancelled) noexcept
    {
        auto * registration_p = static_cast<Self *>(entry_sp->m_owner_p);
        registration_p->m_cancelled = cancelled;
        registration_p->m_stop_callback_opt.reset();
        if (registration_p->m_executor) {
            registration_p->m_queued_owner_sp = std::shared_ptr<Self> {entry_sp, registration_p};
            registration_p->m_executor.enqueue(&registration_p->m_callback);
        } else { registration_p->deliver(); }
    }
    static void invoke(Callback * callback_p) noexcept
    {
        auto * registration_p = static_cast<Self *>(callback_p->m_owner_p);
        auto registration_sp = std::move(registration_p->m_queued_owner_sp);
        auto executor = registration_sp->m_executor;
        registration_sp->deliver();
        executor.workFinished();
    }
    void deliver() noexcept
    {
        if (m_cancelled) { exec::set_stopped(std::move(m_receiver)); }
        else { exec::set_value(std::move(m_receiver)); }
    }
private:
    Receiver m_receiver;
    Executor m_executor;
    TimerEntry m_entry;
    std::chrono::steady_clock::duration m_duration;
    Callback m_callback;
    std::optional<std::stop_callback<Cancel>> m_stop_callback_opt;
    StopRegistrationGate m_stop_gate;
    bool m_cancelled = false;
    std::shared_ptr<Self> m_queued_owner_sp;
};

}
