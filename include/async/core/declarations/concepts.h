#pragma once

#include <async/core/executable/declarations/concepts.h>

#include <concepts>

LCF_ASYNC_EXPORT
namespace lcf::async {

template <typename Record>
concept work_record_c = requires(Record & record) {
    typename Record::executable_t;
    requires executable_c<typename Record::executable_t>;
    { record.getExecutable() } noexcept -> std::same_as<typename Record::executable_t &>;
};

} // namespace lcf::async
