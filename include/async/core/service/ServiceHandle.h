#pragma once

#include <async/config/macros.h>
#include <async/core/service/declarations/enums.h>

#include <chrono>
#include <variant>

LCF_ASYNC_EXPORT
namespace lcf::async {

class ServiceHandle
{
public:
    using clock_t = std::chrono::steady_clock;
    using notification_t = std::variant<ServiceSignal, clock_t::time_point, clock_t::duration>;
    using notification_function_t = void (*)(void *, notification_t) noexcept;
public:
    ~ServiceHandle() noexcept = default;
    ServiceHandle() = delete;
    ServiceHandle(void * state_p, notification_function_t notify_f) noexcept : m_state_p(state_p), m_notify_f(notify_f) {}
    ServiceHandle(const ServiceHandle &) noexcept = default;
    ServiceHandle(ServiceHandle &&) noexcept = default;
    ServiceHandle & operator=(const ServiceHandle &) noexcept = default;
    ServiceHandle & operator=(ServiceHandle &&) noexcept = default;
public:
    void notifyReady() const noexcept { m_notify_f(m_state_p, ServiceSignal::eReady); }
    void wakeAt(clock_t::time_point deadline) const noexcept { m_notify_f(m_state_p, deadline); }
    void wakeAfter(clock_t::duration delay) const noexcept { m_notify_f(m_state_p, delay); }
    void notifyStopped() const noexcept { m_notify_f(m_state_p, ServiceSignal::eStopped); }
private:
    void * m_state_p;
    notification_function_t m_notify_f;
};

} // namespace lcf::async
