#pragma once

#include <async/core/scheduling/ExecutionNode.h>
#include <async/core/scheduling/details/dispatch_targets.h>

#include <cstdint>

LCF_ASYNC_EXPORT
namespace lcf::async {

class DispatchRequest
{
public:
    ~DispatchRequest() noexcept = default;
    DispatchRequest() = delete;
    explicit DispatchRequest(ExecutionNode & executable) noexcept : m_executable(executable) {}
    DispatchRequest(const DispatchRequest &) = delete;
    DispatchRequest(DispatchRequest &&) = delete;
    DispatchRequest & operator=(const DispatchRequest &) = delete;
    DispatchRequest & operator=(DispatchRequest &&) = delete;
public:
    ExecutionNode & getExecutable() const noexcept { return m_executable; }
    std::uint32_t getTargetIndex() const noexcept { return m_target_index; }
    void setTargetIndex(std::uint32_t target_index) noexcept { m_target_index = target_index; }
    DispatchRequest * getNext() const noexcept { return m_next_p; }
    void setNext(DispatchRequest * next_p) noexcept { m_next_p = next_p; }
private:
    ExecutionNode & m_executable;
    std::uint32_t m_target_index = details::k_owner_scheduler_index;
    DispatchRequest * m_next_p = nullptr;
};

} // namespace lcf::async
