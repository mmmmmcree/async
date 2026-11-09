#pragma once

#include <async/core/executable/declarations/concepts.h>

#include <cstdint>

LCF_ASYNC_EXPORT
namespace lcf::async {

template <executable_c Executable>
class DispatchRequest
{
public:
    ~DispatchRequest() noexcept = default;
    DispatchRequest() = delete;
    explicit DispatchRequest(Executable & executable) noexcept : m_executable(executable) {}
    DispatchRequest(const DispatchRequest &) = delete;
    DispatchRequest(DispatchRequest &&) = delete;
    DispatchRequest & operator=(const DispatchRequest &) = delete;
    DispatchRequest & operator=(DispatchRequest &&) = delete;
public:
    Executable & getExecutable() const noexcept { return m_executable; }
    std::uint32_t getTargetIndex() const noexcept { return m_target_index; }
    void setTargetIndex(std::uint32_t target_index) noexcept { m_target_index = target_index; }
    DispatchRequest * getNext() const noexcept { return m_next_p; }
    void setNext(DispatchRequest * next_p) noexcept { m_next_p = next_p; }
private:
    Executable & m_executable;
    std::uint32_t m_target_index = 0;
    DispatchRequest * m_next_p = nullptr;
};

} // namespace lcf::async
