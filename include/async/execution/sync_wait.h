#pragma once

#include "config.h"

// The standard places blocking consumers in this_thread, not execution.
LCF_ASYNC_EXPORT
namespace lcf::async::this_thread {

#if LCF_ASYNC_USE_STD_EXECUTION
using std::this_thread::sync_wait;
#else
using exec::stde::sync_wait;
#endif

} // namespace lcf::async::this_thread
