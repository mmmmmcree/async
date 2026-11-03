#pragma once

#include <async/core/thread_types.h>

#include <async/config/macros.h>

#include <thread>

LCF_ASYNC_EXPORT
namespace lcf::async {

template <typename ThreadHandle>
struct ThreadLauncher;

template <typename Executor, typename ThreadHandle = std::thread>
class BasicWorker;

} // namespace lcf::async
