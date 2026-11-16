#pragma once

#include <async/core/executable/declarations/concepts.h>

LCF_ASYNC_EXPORT
namespace lcf::async {

template <typename Record>
requires executable_c<typename Record::executable_t>
class ExecuteSink
{
public:
    using value_t = Record;
public:
    ~ExecuteSink() noexcept = default;
    ExecuteSink() noexcept = default;
    ExecuteSink(const ExecuteSink &) noexcept = default;
    ExecuteSink(ExecuteSink &&) noexcept = default;
    ExecuteSink & operator=(const ExecuteSink &) noexcept = default;
    ExecuteSink & operator=(ExecuteSink &&) noexcept = default;
public:
    void receive(Record & record) noexcept { record.getExecutable().execute(); }
};

} // namespace lcf::async
