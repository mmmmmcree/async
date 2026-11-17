#pragma once

#include <async/config/macros.h>

#include <cstdint>

LCF_ASYNC_EXPORT
namespace lcf::async {

enum class ExecutionResult : std::uint8_t
{
    eIdle,
    eSucceeded,
    eStopped,
    eFailed
};

} // namespace lcf::async
