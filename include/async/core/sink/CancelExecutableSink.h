#pragma once

#include <async/core/executable/declarations/concepts.h>

LCF_ASYNC_EXPORT
namespace lcf::async {

template <typename Record>
requires cancellable_executable_c<typename Record::executable_t>
class CancelExecutableSink
{
public:
    using value_t = Record;
public:
    ~CancelExecutableSink() noexcept = default;
    CancelExecutableSink() noexcept = default;
    CancelExecutableSink(const CancelExecutableSink &) noexcept = default;
    CancelExecutableSink(CancelExecutableSink &&) noexcept = default;
    CancelExecutableSink & operator=(const CancelExecutableSink &) noexcept = default;
    CancelExecutableSink & operator=(CancelExecutableSink &&) noexcept = default;
public:
    void receive(Record & record) noexcept { record.getExecutable().cancel(); }
};

} // namespace lcf::async
