module;

#include <async/execution/config.h>

export module lcf.async;

// Only this interface translation unit turns public declarations into exports.
#undef LCF_ASYNC_EXPORT
#define LCF_ASYNC_EXPORT export

#include "async/async.h"

#undef LCF_ASYNC_EXPORT
