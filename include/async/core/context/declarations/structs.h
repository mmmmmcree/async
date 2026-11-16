#pragma once

#include <async/config/macros.h>

#include <cstddef>
#include <cstdint>

LCF_ASYNC_EXPORT
namespace lcf::async {

struct SchedulerTargetBinding
{
    bool m_is_shared;
    std::uint32_t m_port_index;
};

struct ContextStorageRequirements
{
    std::size_t m_runtime_size_in_bytes = 0;
    std::size_t m_runtime_alignment = 1;
    std::size_t m_workers_size_in_bytes = 0;
    std::size_t m_workers_alignment = 1;
};

} // namespace lcf::async
