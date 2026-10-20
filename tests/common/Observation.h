#pragma once

#include <Runtime.h>
#include <array>
#include <atomic>
#include <exception>
#include <system_error>

namespace lcf::async::testing {

struct Observation
{
    std::atomic_bool m_done = false;
    std::array<int, 8> m_values {};
    unsigned m_value_count = 0;
    unsigned m_value_calls = 0;
    unsigned m_error_calls = 0;
    unsigned m_stopped_calls = 0;
    std::exception_ptr m_exception;
    std::error_code m_error;
};

struct Receiver
{
    using receiver_concept = exec::receiver_t;
    template <typename... Values>
    void set_value(Values &&... values) noexcept
    {
        ++m_observation_p->m_value_calls;
        ((m_observation_p->m_values[m_observation_p->m_value_count++] = static_cast<int>(values)), ...);
        m_observation_p->m_done.store(true, std::memory_order_release);
    }
    template <typename Error>
    void set_error(Error && error) noexcept
    {
        ++m_observation_p->m_error_calls;
        if constexpr (std::same_as<std::remove_cvref_t<Error>, std::exception_ptr>) { m_observation_p->m_exception = error; }
        else if constexpr (std::same_as<std::remove_cvref_t<Error>, std::error_code>) { m_observation_p->m_error = error; }
        else { m_observation_p->m_exception = std::make_exception_ptr(std::forward<Error>(error)); }
        m_observation_p->m_done.store(true, std::memory_order_release);
    }
    void set_stopped() noexcept
    {
        ++m_observation_p->m_stopped_calls;
        m_observation_p->m_done.store(true, std::memory_order_release);
    }
    Observation * m_observation_p;
};

template <typename Sender>
void run(Context & context, Sender sender, Observation & observation)
{
    auto operation = exec::connect(std::move(sender), Receiver {&observation});
    exec::start(operation);
    pump_until(context, [&] { return observation.m_done.load(std::memory_order_acquire); });
    context.poll();
}

struct StoppedSender
{
    using sender_concept = exec::sender_t;
    using completion_signatures = exec::completion_signatures<exec::set_value_t(), exec::set_stopped_t()>;
    template <typename Receiver>
    struct Operation
    {
        using operation_state_concept = exec::operation_state_t;
        void start() noexcept { exec::set_stopped(std::move(m_receiver)); }
        Receiver m_receiver;
    };
    template <typename Receiver>
    auto connect(Receiver receiver) const { return Operation<Receiver> {std::move(receiver)}; }
};

}
