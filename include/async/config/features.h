#pragma once

#ifndef LCF_ASYNC_USE_STD_EXECUTION
#include <version>
#ifdef __cpp_lib_senders
#define LCF_ASYNC_USE_STD_EXECUTION 1
#else
#define LCF_ASYNC_USE_STD_EXECUTION 0
#endif
#endif
