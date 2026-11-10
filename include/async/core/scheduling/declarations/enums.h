#pragma once

#include <async/config/macros.h>

#include <cstdint>

LCF_ASYNC_EXPORT
namespace lcf::async {

enum PollResult : std::uint8_t
{
    eIdle = 0,
    eDispatched = 1,
    eExecuted = 2,
};

} // namespace lcf::async
