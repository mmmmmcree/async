#pragma once

#include "../config.h"

LCF_ASYNC_EXPORT
namespace lcf::async::exec {

using stde::get_env_t;
using stde::get_env;
using stde::env_of_t;
using stde::get_stop_token_t;
using stde::get_stop_token;
using stde::get_allocator_t;
using stde::get_allocator;
using stde::get_scheduler_t;
using stde::get_scheduler;
using stde::get_completion_scheduler_t;
using stde::get_completion_scheduler;

} // namespace lcf::async::exec
