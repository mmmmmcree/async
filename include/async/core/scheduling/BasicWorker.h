#pragma once

#include <async/core/thread_types.h>

#include <async/config/macros.h>

#include <thread>

LCF_ASYNC_EXPORT
namespace lcf::async {

struct StdThreadFactory;

template <typename Executor, typename Thread = std::thread>
class BasicWorker;

} // namespace lcf::async
