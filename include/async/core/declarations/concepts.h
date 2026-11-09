#pragma once

#include <async/core/executable/declarations/concepts.h>

#include <type_traits>

LCF_ASYNC_EXPORT
namespace lcf::async {

template <typename Record>
concept work_record_c = requires(Record & record) {
    { record.getExecutable() } noexcept;
    requires std::is_lvalue_reference_v<decltype(record.getExecutable())>;
    requires executable_c<std::remove_reference_t<decltype(record.getExecutable())>>;
};

} // namespace lcf::async
