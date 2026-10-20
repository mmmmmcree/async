#pragma once

#include <async/core/protocol.h>
#include <exec/split.hpp>

namespace lcf::async::exec {

LCF_ASYNC_EXPORT
using stde::just;

LCF_ASYNC_EXPORT
using stde::just_error;

LCF_ASYNC_EXPORT
using stde::just_stopped;

LCF_ASYNC_EXPORT
using stde::then;

LCF_ASYNC_EXPORT
using stde::let_value;

LCF_ASYNC_EXPORT
using stde::when_all;

LCF_ASYNC_EXPORT
using stde::on;

LCF_ASYNC_EXPORT
using stde::starts_on;

LCF_ASYNC_EXPORT
using stde::continues_on;

LCF_ASYNC_EXPORT
using stde::sync_wait;

LCF_ASYNC_EXPORT
using ::exec::split;

LCF_ASYNC_EXPORT
using stde::upon_error;
LCF_ASYNC_EXPORT
using stde::upon_stopped;
LCF_ASYNC_EXPORT
using stde::let_error;
LCF_ASYNC_EXPORT
using stde::let_stopped;

}

#include <async/execution/error_handling.h>
#include <async/execution/environment.h>
