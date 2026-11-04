#pragma once

#include <async/config/macros.h>

LCF_ASYNC_EXPORT
namespace lcf::async {

enum class EnqueueResult
{
    eAccepted,
    eClosed,
};

} // namespace lcf::async
