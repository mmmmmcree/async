#pragma once

#include "Executor.h"

namespace lcf::async {

LCF_ASYNC_EXPORT
template <typename Function>
void post(Executor executor, Function function) { executor.post(std::move(function)); }

}
