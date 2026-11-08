#pragma once

#include <cstdint>
#include <limits>

namespace lcf::async::details {

inline constexpr std::uint32_t k_unbound_domain_id = 0;
inline constexpr std::uint32_t k_owner_scheduler_index = std::numeric_limits<std::uint32_t>::max();

} // namespace lcf::async::details
