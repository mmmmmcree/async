#pragma once
#include <async/config/macros.h>
#include <chrono>
LCF_ASYNC_EXPORT
namespace lcf::async {
class ServiceHandle
{
public:
    using clock_t = std::chrono::steady_clock;
    enum class Notification { eReady, eTimer, eStopped };
    using notification_function_t = void (*)(void *, Notification, clock_t::time_point) noexcept;
    ~ServiceHandle() noexcept = default;
    ServiceHandle() = delete;
    ServiceHandle(void * state_p, notification_function_t notify_f) noexcept : m_state_p(state_p), m_notify_f(notify_f) {}
    ServiceHandle(const ServiceHandle &) noexcept = default;
    ServiceHandle(ServiceHandle &&) noexcept = default;
    ServiceHandle & operator=(const ServiceHandle &) noexcept = default;
    ServiceHandle & operator=(ServiceHandle &&) noexcept = default;
    void notifyReady() const noexcept { m_notify_f(m_state_p, Notification::eReady, {}); }
    void setWakeTime(clock_t::time_point time) const noexcept { m_notify_f(m_state_p, Notification::eTimer, time); }
    void notifyStopped() const noexcept { m_notify_f(m_state_p, Notification::eStopped, {}); }
private:
    void * m_state_p;
    notification_function_t m_notify_f;
};
}
