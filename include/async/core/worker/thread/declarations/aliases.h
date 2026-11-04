#pragma once

#include <async/config/macros.h>

LCF_ASYNC_EXPORT
namespace lcf::async {

using ThreadEntry = void (*)(void *) noexcept;

} // namespace lcf::async
