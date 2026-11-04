#pragma once

#include <async/config/macros.h>

LCF_ASYNC_EXPORT
namespace lcf::async {

enum class WorkerState
{
    eIdle,
    eSearching,
    eExecuting,
    eWaiting,
    eStopped,
};

} // namespace lcf::async
