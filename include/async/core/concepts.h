#pragma once

#include "../config/macros.h"
#include "enums.h"

LCF_ASYNC_EXPORT
namespace lcf::async {

using ThreadEntry = void (*)(void *) noexcept;

// Concepts require definitions: schedule_record_c, executor_c,
// schedule_queue_c, blocking_schedule_queue_c, stoppable_schedule_queue_c,
// event_loop_c, blocking_event_loop_c, stoppable_event_loop_c,
// event_loop_factory_c, thread_c, thread_factory_c.

} // namespace lcf::async
