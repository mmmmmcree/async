#pragma once

#include <async/config/macros.h>
#include "enums.h"
#include <async/protocol.h>
#include <async/core/thread_types.h>

LCF_ASYNC_EXPORT
namespace lcf::async {

template <typename Type>
concept sender_c = protocols::sender<Type>;

template <typename Type>
concept receiver_c = protocols::receiver<Type>;

template <typename Type>
concept scheduler_c = protocols::scheduler<Type>;

template <typename Type>
concept operation_state_c = protocols::operation_state<Type>;

template <typename Receiver, typename Completions>
concept receiver_of_c = protocols::receiver_of<Receiver, Completions>;

// Concepts require definitions: schedule_node_c, schedule_record_c, executor_c,
// work_executor_c, blocking_work_executor_c, multi_worker_executor_c,
// stoppable_work_executor_c, schedule_queue_c, blocking_schedule_queue_c,
// stoppable_schedule_queue_c, work_executor_factory_c, thread_c, thread_factory_c.

} // namespace lcf::async
