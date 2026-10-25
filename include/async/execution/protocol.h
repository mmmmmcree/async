#pragma once

#include <async/execution/config.h>

// Public execution vocabulary; independent of async's runtime mechanisms.
namespace lcf::async::exec {

using stde::sender_t;
using stde::receiver_t;
using stde::operation_state_t;

using stde::set_value_t;
using stde::set_error_t;
using stde::set_stopped_t;
using stde::set_value;
using stde::set_error;
using stde::set_stopped;

using stde::get_env_t;
using stde::get_env;
using stde::get_stop_token_t;
using stde::get_stop_token;
using stde::get_allocator_t;
using stde::get_allocator;
using stde::get_scheduler_t;
using stde::get_scheduler;
using stde::get_completion_scheduler_t;
using stde::get_completion_scheduler;

using stde::completion_signatures;
using stde::completion_signatures_of_t;
using stde::env_of_t;
using stde::sender;
using stde::receiver;
using stde::operation_state;
using stde::sender_to;
using stde::scheduler;

using stde::connect_t;
using stde::connect;
using stde::connect_result_t;
using stde::start_t;
using stde::start;
using stde::schedule_t;
using stde::schedule;
using stde::schedule_result_t;

} // namespace lcf::async::exec
