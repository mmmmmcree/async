#pragma once

#include <async/config/macros.h>

#include <cstddef>

LCF_ASYNC_EXPORT
namespace lcf::async {

struct StorageRequirements
{
    std::size_t m_size_in_bytes = 0;
    std::size_t m_alignment = 1;
};

} // namespace lcf::async
