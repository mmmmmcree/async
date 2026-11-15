#pragma once
#include <async/config/macros.h>
#include <async/core/driver/BasicDriver.h>
#include <async/core/dispatcher/declarations/structs.h>
#include <atomic>
LCF_ASYNC_EXPORT
namespace lcf::async {
template <retry_stream_c Stream, rejecting_sink_c Sink>
requires std::same_as<typename Stream::value_t, typename Sink::value_t>
class BasicDispatcher
{
public:
    ~BasicDispatcher() noexcept = default;
    BasicDispatcher() = delete;
    BasicDispatcher(Stream & stream, Sink sink) : m_driver(stream, std::move(sink)) {}
    BasicDispatcher(const BasicDispatcher &) = delete;
    BasicDispatcher(BasicDispatcher &&) = delete;
    BasicDispatcher & operator=(const BasicDispatcher &) = delete;
    BasicDispatcher & operator=(BasicDispatcher &&) = delete;
    DispatchReport dispatchOne() noexcept
    {
        if (m_driving.test_and_set(std::memory_order_acquire)) { return {}; }
        const DriveReport report = m_driver.driveOne();
        m_driving.clear(std::memory_order_release);
        return {.m_dispatched = report.hasProgress(), .m_error_code = report.m_error_code};
    }
private:
    BasicDriver<Stream, Sink> m_driver;
    std::atomic_flag m_driving {};
};
}
