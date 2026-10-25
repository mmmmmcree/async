#pragma once

#ifndef LCF_ASYNC_USE_STD_EXECUTION
#include <version>
#ifdef __cpp_lib_senders
#define LCF_ASYNC_USE_STD_EXECUTION 1
#else
#define LCF_ASYNC_USE_STD_EXECUTION 0
#endif
#endif

#if LCF_ASYNC_USE_STD_EXECUTION
#include <execution>
namespace lcf::async::details {
namespace execution = std::execution;
}
#else
#include <stdexec/execution.hpp>
namespace lcf::async::details {
namespace execution = stdexec;
}
#endif
