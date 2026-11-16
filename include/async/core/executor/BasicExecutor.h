#pragma once

#include <async/config/macros.h>
#include <async/core/driver/BasicDriver.h>
#include <async/core/executor/declarations/structs.h>

LCF_ASYNC_EXPORT
namespace lcf::async {

template <stream_c Stream, sink_c Sink>
requires (not rejecting_sink_c<Sink>) and std::same_as<typename Stream::value_t, typename Sink::value_t>
class BasicExecutor
{
public:
    ~BasicExecutor() noexcept = default;
    BasicExecutor() = delete;
    BasicExecutor(Stream & stream, Sink sink) : m_driver(stream, std::move(sink)) {}
    BasicExecutor(const BasicExecutor &) = default;
    BasicExecutor(BasicExecutor &&) = default;
    BasicExecutor & operator=(const BasicExecutor &) = default;
    BasicExecutor & operator=(BasicExecutor &&) = default;
public:
    ExecutionReport executeOne() noexcept
    {
        const DriveReport report = m_driver.driveOne();
        return {.m_result = report.hasProgress() ? ExecutionResult::eProgress : ExecutionResult::eIdle,
            .m_executed = report.hasProgress(), .m_error_code = report.m_error_code};
    }
private:
    BasicDriver<Stream, Sink> m_driver;
};

} // namespace lcf::async
