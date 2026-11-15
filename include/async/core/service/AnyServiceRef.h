#pragma once
#include <async/config/macros.h>
#include <async/core/service/declarations/concepts.h>
#include <memory>
LCF_ASYNC_EXPORT
namespace lcf::async {
class AnyServiceRef
{
public:
    ~AnyServiceRef() noexcept = default;
    AnyServiceRef() = delete;
    template <service_c Service> requires (not std::same_as<Service, AnyServiceRef>)
    explicit AnyServiceRef(Service & service) noexcept : m_service_p(std::addressof(service)),
        m_start_f([](void * p, ServiceHandle h) noexcept { return static_cast<Service *>(p)->start(h); }),
        m_drive_f([](void * p) noexcept { return static_cast<Service *>(p)->driveOne(); }),
        m_stop_f([](void * p) noexcept { static_cast<Service *>(p)->requestStop(); }) {}
    AnyServiceRef(const AnyServiceRef &) noexcept = default;
    AnyServiceRef(AnyServiceRef &&) noexcept = default;
    AnyServiceRef & operator=(const AnyServiceRef &) noexcept = default;
    AnyServiceRef & operator=(AnyServiceRef &&) noexcept = default;
    std::error_code start(ServiceHandle handle) noexcept { return m_start_f(m_service_p, handle); }
    DriveReport driveOne() noexcept { return m_drive_f(m_service_p); }
    void requestStop() noexcept { m_stop_f(m_service_p); }
private:
    void * m_service_p;
    std::error_code (*m_start_f)(void *, ServiceHandle) noexcept;
    DriveReport (*m_drive_f)(void *) noexcept;
    void (*m_stop_f)(void *) noexcept;
};
}
