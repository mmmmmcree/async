#pragma once

#include "../../config/macros.h"
#include "queue/MutexInbox.h"

LCF_ASYNC_EXPORT
namespace lcf::async {

template <typename Queue = MutexInbox>
class BasicEventLoop;

template <typename EventLoop>
struct DefaultEventLoopFactory;

} // namespace lcf::async
