#pragma once

#include <async/core/timer/operations.h>

namespace lcf::async {

LCF_ASYNC_EXPORT
inline DelaySender schedule_after(Scheduler scheduler, std::chrono::steady_clock::duration duration)
{ return {duration, std::pmr::get_default_resource(), Executor {std::move(scheduler)}}; }

}
