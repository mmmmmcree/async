#pragma once

#include <async/config/execution_provider.h>

LCF_ASYNC_EXPORT
namespace lcf::async::exec {

using stde::connect_t;
using stde::connect;
using stde::connect_result_t;
using stde::start_t;
using stde::start;
using stde::schedule_t;
using stde::schedule;
using stde::schedule_result_t;

using stde::set_value_t;
using stde::set_value;
using stde::set_error_t;
using stde::set_error;
using stde::set_stopped_t;
using stde::set_stopped;

} // namespace lcf::async::exec
