#pragma once

#include "../../config/macros.h"

#include <thread>

LCF_ASYNC_EXPORT
namespace lcf::async {

struct StdThreadFactory;

template <typename EventLoop, typename Thread = std::thread>
class BasicWorker;

} // namespace lcf::async
