#pragma once

#include "../../config/macros.h"
#include "../scheduling/BasicEventLoop.h"

#include <thread>

LCF_ASYNC_EXPORT
namespace lcf::async {

template <typename EventLoop = BasicEventLoop<>, typename Thread = std::thread>
class BasicContext;

using Context = BasicContext<>;

} // namespace lcf::async
