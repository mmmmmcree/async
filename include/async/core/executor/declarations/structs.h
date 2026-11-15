#pragma once

#include <async/config/macros.h>
#include <cstdint>
#include <system_error>

LCF_ASYNC_EXPORT
namespace lcf::async {

class ExecutionResult
{
public:
    enum State : std::uint8_t { eIdle, eProgress, eStopped };
    ~ExecutionResult() noexcept = default;
    constexpr ExecutionResult(State state = eIdle) noexcept : m_state(state) {}
    ExecutionResult(const ExecutionResult &) noexcept = default;
    ExecutionResult(ExecutionResult &&) noexcept = default;
    ExecutionResult & operator=(const ExecutionResult &) noexcept = default;
    ExecutionResult & operator=(ExecutionResult &&) noexcept = default;
    constexpr operator bool() const noexcept { return m_state == eProgress; }
    constexpr bool operator==(const ExecutionResult &) const noexcept = default;
    constexpr bool operator==(State state) const noexcept { return m_state == state; }
private:
    State m_state;
};

struct ExecutionReport
{
    constexpr operator ExecutionResult() const noexcept { return m_result; }
    constexpr operator bool() const noexcept { return m_result == ExecutionResult::eProgress; }
    constexpr bool hasProgress() const noexcept { return m_result == ExecutionResult::eProgress; }
    constexpr bool isStopped() const noexcept { return m_result == ExecutionResult::eStopped; }

    ExecutionResult m_result = ExecutionResult::eIdle;
    std::uint64_t m_executed = 0;
    std::error_code m_error_code {};
};

} // namespace lcf::async
