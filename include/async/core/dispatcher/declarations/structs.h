#pragma once
#include <async/config/macros.h>
#include <cstdint>
#include <system_error>
LCF_ASYNC_EXPORT
namespace lcf::async {
struct DispatchReport
{
    std::uint64_t m_dispatched = 0;
    std::error_code m_error_code {};
    bool hasProgress() const noexcept { return m_dispatched != 0; }
    explicit operator bool() const noexcept { return this->hasProgress(); }
};
}
