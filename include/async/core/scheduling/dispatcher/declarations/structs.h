#pragma once

#include <async/config/macros.h>

#include <cstddef>
#include <system_error>

LCF_ASYNC_EXPORT
namespace lcf::async {

struct DispatchReport
{
    operator std::error_code() const noexcept { return m_error_code; }
    constexpr bool hasProgress() const noexcept { return m_received or m_dispatched; }

    std::size_t m_received = 0;
    std::size_t m_dispatched = 0;
    std::error_code m_error_code {};
};

} // namespace lcf::async
