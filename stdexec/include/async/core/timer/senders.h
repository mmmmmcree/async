#pragma once

#include <async/core/scheduling/Scheduler.h>
#include <async/core/timer/operations.h>

namespace lcf::async {

LCF_ASYNC_EXPORT
inline auto schedule_after(Scheduler scheduler, std::chrono::steady_clock::duration duration)
{ return stde::continues_on(delay(duration), std::move(scheduler)); }

}
