#pragma once

#include <async/execution/protocol.h>
#include <async/execution/sync_wait.h>

namespace lcf::async::exec {

// Keep provider types and customization points intact; do not wrap algorithms.
using stde::just;
using stde::just_error;
using stde::just_stopped;
using stde::then;
using stde::upon_error;
using stde::upon_stopped;
using stde::let_value;
using stde::let_error;
using stde::let_stopped;
using stde::when_all;
using stde::starts_on;
using stde::continues_on;
using stde::on;

namespace ext {}

} // namespace lcf::async::exec
