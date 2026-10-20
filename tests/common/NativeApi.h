#pragma once

#if defined(LCF_ASYNC_TEST_USE_MODULES) && LCF_ASYNC_TEST_USE_MODULES
#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <concepts>
#include <condition_variable>
#include <coroutine>
#include <cstddef>
#include <exception>
#include <expected>
#include <functional>
#include <limits>
#include <memory>
#include <memory_resource>
#include <mutex>
#include <new>
#include <optional>
#include <stop_token>
#include <string>
#include <system_error>
#include <thread>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>
import lcf.async;
#else
#include <async.h>
#endif
