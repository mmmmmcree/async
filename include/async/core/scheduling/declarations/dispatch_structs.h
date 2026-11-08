#pragma once

#include <async/core/queue/ExecutionNode.h>

#include <cstdint>
#include <limits>

LCF_ASYNC_EXPORT
namespace lcf::async {

struct DispatchRequest
{
    queue::ExecutionNode * m_executable_p;
    std::uint32_t m_target_index = std::numeric_limits<std::uint32_t>::max();
    DispatchRequest * m_next_p = nullptr;
    DispatchRequest * getNext() const noexcept { return m_next_p; }
    void setNext(DispatchRequest * next_p) noexcept { m_next_p = next_p; }
    void markQueued() noexcept { m_next_p = nullptr; }
    void clearQueueState() noexcept { m_next_p = nullptr; }
};

} // namespace lcf::async
