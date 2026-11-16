#pragma once

#include <async/core/executable/declarations/concepts.h>

LCF_ASYNC_EXPORT
namespace lcf::async {

template <typename Record>
requires cancellable_executable_c<typename Record::executable_t>
class CancelSink
{
public:
    using value_t = Record;
public:
    ~CancelSink() noexcept = default;
    CancelSink() noexcept = default;
    CancelSink(const CancelSink &) noexcept = default;
    CancelSink(CancelSink &&) noexcept = default;
    CancelSink & operator=(const CancelSink &) noexcept = default;
    CancelSink & operator=(CancelSink &&) noexcept = default;
public:
    void receive(Record & record) noexcept { record.getExecutable().cancel(); }
};

} // namespace lcf::async
