#pragma once

#include <async/config.h>

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
#else
#include <stdexec/execution.hpp>
#endif

namespace lcf::async::exec {

#if LCF_ASYNC_USE_STD_EXECUTION
namespace stde = ::std::execution;
#else
namespace stde = ::stdexec;
#endif

} // namespace lcf::async::exec
