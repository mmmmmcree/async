#pragma once

#include <async/config/macros.h>
#include <async/config/features.h>

#if LCF_ASYNC_USE_STD_EXECUTION
#include <execution>
#else
#include <stdexec/execution.hpp>
#endif

LCF_ASYNC_EXPORT
namespace lcf::async {

#if LCF_ASYNC_USE_STD_EXECUTION
namespace stde = ::std::execution;
#else
namespace stde = ::stdexec;
#endif

} // namespace lcf::async
