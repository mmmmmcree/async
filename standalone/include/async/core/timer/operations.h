#pragma once

#include <async/core/timer/details/timer_wait.h>

namespace lcf::async {

LCF_ASYNC_EXPORT
struct DelaySender
{
    using sender_concept = exec::sender_t;
    using completion_signatures = exec::completion_signatures<exec::set_value_t(), exec::set_error_t(std::exception_ptr), exec::set_stopped_t()>;
    template <typename Receiver>
    struct Operation
    {
        using operation_state_concept = exec::operation_state_t;
        void start() noexcept { auto registration_sp = std::move(m_registration_sp); registration_sp->start(registration_sp); }
        std::shared_ptr<details::TimerRegistration<Receiver>> m_registration_sp;
    };
    template <exec::receiver Receiver>
    auto connect(Receiver receiver) const
    {
        using Registration = details::TimerRegistration<Receiver>;
        return Operation<Receiver> {std::allocate_shared<Registration>(std::pmr::polymorphic_allocator<Registration> {m_resource_p},
            std::move(receiver), m_duration, m_executor)};
    }
    std::chrono::steady_clock::duration m_duration;
    std::pmr::memory_resource * m_resource_p;
    Executor m_executor;
};

LCF_ASYNC_EXPORT
inline DelaySender delay(std::chrono::steady_clock::duration duration,
    std::pmr::memory_resource * resource_p = std::pmr::get_default_resource()) noexcept { return {duration, resource_p, {}}; }

}
