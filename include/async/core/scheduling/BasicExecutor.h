#pragma once

#include <async/config/macros.h>
#include <async/core/scheduling/queue/MutexInbox.h>

LCF_ASYNC_EXPORT
namespace lcf::async {

template <typename Queue = MutexInbox>
class BasicExecutor;

template <typename Executor>
struct DefaultExecutorFactory;

} // namespace lcf::async
