module;

#include <algorithm>
#include <cstdint>
#include <chrono>
#include <condition_variable>
#include <memory_resource>
#include <mutex>
#include <optional>
#include <span>
#include <stdexcept>
#include <system_error>
#include <thread>

#include <async/config/execution_provider.h>

export module lcf.async;

// Only this interface translation unit turns public declarations into exports.
#undef LCF_ASYNC_EXPORT
#define LCF_ASYNC_EXPORT export

// Project headers intentionally attach their public declarations to this module.
#if defined(__clang__)
#pragma clang diagnostic push
#if __has_warning("-Winclude-angled-in-module-purview")
#pragma clang diagnostic ignored "-Winclude-angled-in-module-purview"
#endif
#endif

#include <async/async.h>

#if defined(__clang__)
#pragma clang diagnostic pop
#endif

#undef LCF_ASYNC_EXPORT
