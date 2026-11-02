#pragma once

#include <async/config/macros.h>
#include <async/core/scheduling/BasicExecutor.h>

#include <thread>

LCF_ASYNC_EXPORT
namespace lcf::async {

template <typename Executor = BasicExecutor<>, typename Thread = std::thread>
class BasicContext;

using Context = BasicContext<>;

} // namespace lcf::async
