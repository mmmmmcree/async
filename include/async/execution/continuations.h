#pragma once

#include <async/config/execution_provider.h>

LCF_ASYNC_EXPORT
namespace lcf::async::exec {

using stde::then;
using stde::upon_error;
using stde::upon_stopped;
using stde::let_value;
using stde::let_error;
using stde::let_stopped;

} // namespace lcf::async::exec
