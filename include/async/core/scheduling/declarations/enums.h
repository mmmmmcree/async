#pragma once

#include <async/config/macros.h>

#include <cstddef>
#include <cstdint>

LCF_ASYNC_EXPORT
namespace lcf::async {

enum class Priority : std::uint8_t { eHigh, eNormal, eLow };

inline constexpr std::size_t k_priority_count = 3;

} // namespace lcf::async
