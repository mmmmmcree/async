#pragma once

#include <async/stdexec.h>

namespace lcf::async::exec {

LCF_ASYNC_EXPORT
using stde::sender_t;
LCF_ASYNC_EXPORT
using stde::receiver_t;
LCF_ASYNC_EXPORT
using stde::operation_state_t;
LCF_ASYNC_EXPORT
using stde::set_value_t;
LCF_ASYNC_EXPORT
using stde::set_error_t;
LCF_ASYNC_EXPORT
using stde::set_stopped_t;
LCF_ASYNC_EXPORT
using stde::set_value;
LCF_ASYNC_EXPORT
using stde::set_error;
LCF_ASYNC_EXPORT
using stde::set_stopped;
LCF_ASYNC_EXPORT
using stde::get_env_t;
LCF_ASYNC_EXPORT
using stde::get_env;
LCF_ASYNC_EXPORT
using stde::get_stop_token_t;
LCF_ASYNC_EXPORT
using stde::get_stop_token;
LCF_ASYNC_EXPORT
using stde::get_allocator_t;
LCF_ASYNC_EXPORT
using stde::get_allocator;
LCF_ASYNC_EXPORT
using stde::get_scheduler_t;
LCF_ASYNC_EXPORT
using stde::get_scheduler;
LCF_ASYNC_EXPORT
using stde::get_completion_scheduler_t;
LCF_ASYNC_EXPORT
using stde::get_completion_scheduler;
LCF_ASYNC_EXPORT
using stde::completion_signatures;
LCF_ASYNC_EXPORT
using stde::completion_signatures_of_t;
LCF_ASYNC_EXPORT
using stde::env_of_t;
LCF_ASYNC_EXPORT
using stde::sender;
LCF_ASYNC_EXPORT
using stde::receiver;
LCF_ASYNC_EXPORT
using stde::operation_state;
LCF_ASYNC_EXPORT
using stde::connect_t;
LCF_ASYNC_EXPORT
using stde::connect;
LCF_ASYNC_EXPORT
using stde::start_t;
LCF_ASYNC_EXPORT
using stde::start;
LCF_ASYNC_EXPORT
using stde::schedule_t;
LCF_ASYNC_EXPORT
using stde::schedule;
LCF_ASYNC_EXPORT
using stde::connect_result_t;
LCF_ASYNC_EXPORT
using stde::sender_to;
LCF_ASYNC_EXPORT
using stde::scheduler;
LCF_ASYNC_EXPORT
using stde::prop;
LCF_ASYNC_EXPORT
using empty_env = stde::env<>;

}
