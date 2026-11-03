#pragma once

#include <async/config/macros.h>
#include <async/core/queue/intrusive/mpmc/Queue.h>

LCF_ASYNC_EXPORT
namespace lcf::async {

template <typename Queue = queue::intrusive::mpmc::Queue<>>
class BasicExecutor;

template <typename Executor>
struct DefaultContextExecutorFactory;

} // namespace lcf::async
