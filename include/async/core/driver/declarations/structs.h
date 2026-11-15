#pragma once

#include <async/config/macros.h>

#include <system_error>

LCF_ASYNC_EXPORT
namespace lcf::async {

struct DriveReport
{
    bool m_has_progress = false;
    std::error_code m_error_code {};
    bool hasProgress() const noexcept { return m_has_progress; }
    explicit operator bool() const noexcept { return this->hasProgress(); }
};

} // namespace lcf::async
