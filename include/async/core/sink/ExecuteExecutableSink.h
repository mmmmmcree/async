#pragma once

#include <async/core/executable/declarations/concepts.h>

LCF_ASYNC_EXPORT
namespace lcf::async {

template <typename Record>
requires executable_c<typename Record::executable_t>
class ExecuteExecutableSink
{
public:
    using value_t = Record;
public:
    ~ExecuteExecutableSink() noexcept = default;
    ExecuteExecutableSink() noexcept = default;
    ExecuteExecutableSink(const ExecuteExecutableSink &) noexcept = default;
    ExecuteExecutableSink(ExecuteExecutableSink &&) noexcept = default;
    ExecuteExecutableSink & operator=(const ExecuteExecutableSink &) noexcept = default;
    ExecuteExecutableSink & operator=(ExecuteExecutableSink &&) noexcept = default;
public:
    void receive(Record & record) noexcept { record.getExecutable().execute(); }
};

} // namespace lcf::async
