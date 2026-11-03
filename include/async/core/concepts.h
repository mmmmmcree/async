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

// Concepts require definitions: executable_c, work_record_c, record_submitter_c,
// work_executor_c, waiting_work_executor_c, multi_worker_executor_c,
// stoppable_work_executor_c, work_queue_c, waiting_work_queue_c,
// stoppable_work_queue_c, context_executor_factory_c, join_handle_c, thread_launcher_c.

} // namespace lcf::async
