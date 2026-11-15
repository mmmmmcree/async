#pragma once
#include <async/core/driver/declarations/concepts.h>
LCF_ASYNC_EXPORT
namespace lcf::async {
template <driver_c Driver>
class DriverHandle
{
public:
    ~DriverHandle() noexcept = default;
    DriverHandle() = delete;
    explicit DriverHandle(Driver & driver) noexcept : m_driver_p(&driver) {}
    DriverHandle(const DriverHandle &) noexcept = default;
    DriverHandle(DriverHandle &&) noexcept = default;
    DriverHandle & operator=(const DriverHandle &) noexcept = default;
    DriverHandle & operator=(DriverHandle &&) noexcept = default;
    DriveReport driveOne() noexcept { return m_driver_p->driveOne(); }
private:
    Driver * m_driver_p;
};
}
