#pragma once

#include <async/config/macros.h>
#include <async/core/executor/declarations/enums.h>

#include <cstdint>
#include <system_error>

LCF_ASYNC_EXPORT
namespace lcf::async {

class ExecutionReport
{
public:
    ~ExecutionReport() noexcept = default;
    ExecutionReport() noexcept = default;
    ExecutionReport(const ExecutionReport &) noexcept = default;
    ExecutionReport(ExecutionReport &&) noexcept = default;
    ExecutionReport & operator=(const ExecutionReport &) noexcept = default;
    ExecutionReport & operator=(ExecutionReport &&) noexcept = default;
public:
    static ExecutionReport make_from_succeeded(std::uint64_t executed_count = 0) noexcept
    { return {ExecutionResult::eSucceeded, executed_count, {}}; }
    static ExecutionReport make_from_stopped(std::uint64_t executed_count = 0) noexcept
    { return {ExecutionResult::eStopped, executed_count, {}}; }
    static ExecutionReport make_from_error_code(std::error_code ec, std::uint64_t executed_count = 0) noexcept
    {
        return {ec ? ExecutionResult::eFailed : ExecutionResult::eSucceeded, executed_count, ec};
    }
public:
    constexpr ExecutionResult getResult() const noexcept { return m_result; }
    constexpr operator ExecutionResult() const noexcept { return m_result; }
    constexpr operator bool() const noexcept { return this->isSucceeded(); }
    constexpr bool isSucceeded() const noexcept { return m_result == ExecutionResult::eSucceeded; }
    constexpr bool isStopped() const noexcept { return m_result == ExecutionResult::eStopped; }
    constexpr bool isFailed() const noexcept { return m_result == ExecutionResult::eFailed; }
    constexpr std::uint64_t getExecutedCount() const noexcept { return m_executed; }
    std::error_code getErrorCode() const noexcept { return m_error_code; }
private:
    ExecutionReport(ExecutionResult result, std::uint64_t executed_count, std::error_code ec) noexcept :
        m_result(result), m_executed(executed_count), m_error_code(ec) {}
private:
    ExecutionResult m_result = ExecutionResult::eIdle;
    std::uint64_t m_executed = 0;
    std::error_code m_error_code {};
};

} // namespace lcf::async
