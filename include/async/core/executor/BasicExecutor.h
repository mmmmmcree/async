#pragma once

#include <async/config/macros.h>
#include <async/core/driver/BasicDriver.h>
#include <async/core/executor/declarations/structs.h>

LCF_ASYNC_EXPORT
namespace lcf::async {

template <stream_c Stream, sink_c Sink>
requires matched_c<Stream, Sink> and (not rejecting_sink_c<Sink>)
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
        if (const std::error_code ec = report.getErrorCode()) { return ExecutionReport::make_from_error_code(ec); }
        return report.hasProgress() ? ExecutionReport::make_from_succeeded(1) : ExecutionReport {};
    }
private:
    BasicDriver<Stream, Sink> m_driver;
};

} // namespace lcf::async
