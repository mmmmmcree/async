#pragma once

#include <async/config/macros.h>
#include <async/core/error.h>

#include <cstdint>
#include <system_error>

LCF_ASYNC_EXPORT
namespace lcf::async {

class DispatchReport
{
public:
    ~DispatchReport() noexcept = default;
    DispatchReport() noexcept = default;
    DispatchReport(const DispatchReport &) noexcept = default;
    DispatchReport(DispatchReport &&) noexcept = default;
    DispatchReport & operator=(const DispatchReport &) noexcept = default;
    DispatchReport & operator=(DispatchReport &&) noexcept = default;
public:
    static DispatchReport make_from_succeeded(std::uint64_t dispatched_count = 1) noexcept
    { return {dispatched_count, {}}; }
    static DispatchReport make_from_error_code(std::error_code ec) noexcept
    { return {0, ec}; }
    static DispatchReport make_from_drive_result(std::error_code ec) noexcept
    {
        if (not ec) { return make_from_succeeded(); }
        if (ec == errc::no_work or ec == std::errc::no_buffer_space) { return {}; }
        return make_from_error_code(ec);
    }
public:
    constexpr bool isSucceeded() const noexcept { return m_dispatched != 0; }
    bool isFailed() const noexcept { return static_cast<bool>(m_error_code); }
    bool isIdle() const noexcept { return not this->isSucceeded() and not this->isFailed(); }
    constexpr explicit operator bool() const noexcept { return this->isSucceeded(); }
    constexpr std::uint64_t getDispatchedCount() const noexcept { return m_dispatched; }
    std::error_code getErrorCode() const noexcept { return m_error_code; }
private:
    DispatchReport(std::uint64_t dispatched_count, std::error_code ec) noexcept :
        m_dispatched(dispatched_count), m_error_code(ec) {}
private:
    std::uint64_t m_dispatched = 0;
    std::error_code m_error_code {};
};

} // namespace lcf::async
