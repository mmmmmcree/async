#pragma once

#include <async/config/macros.h>

#include <cstdint>
#include <span>

LCF_ASYNC_EXPORT
namespace lcf::async {

enum class SchedulingTargetKind { eShared, eThread };

struct TargetRoute
{
    SchedulingTargetKind m_kind;
    std::uint64_t m_queue_index = 0;
    std::uint64_t m_dispatcher_index = 0;
};

class RouteTable
{
public:
    explicit RouteTable(std::span<const TargetRoute> target_routes) noexcept : m_target_routes(target_routes) {}
    const TargetRoute & operator[](std::uint32_t target_index) const noexcept { return m_target_routes[target_index]; }
private:
    std::span<const TargetRoute> m_target_routes;
};

} // namespace lcf::async
