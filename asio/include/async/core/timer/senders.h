#pragma once

#include <async/core/coroutine/operations.h>
#include <async/core/timer/operations.h>

namespace lcf::async {

LCF_ASYNC_EXPORT
inline auto schedule_after(Scheduler scheduler, std::chrono::steady_clock::duration duration)
{ return as_sender(std::move(scheduler), delay(duration)); }

}
