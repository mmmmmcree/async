#pragma once

#include <async/core/context/declarations/enums.h>

#include <cstdint>

namespace lcf::async::details {

struct TargetRoute
{
    SchedulingTargetKind m_target_kind;
    std::uint64_t m_thread_execution_queue_index = 0;
};

} // namespace lcf::async::details
