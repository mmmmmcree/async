#pragma once

#include <async/config.h>

#include <asio/any_io_executor.hpp>

namespace lcf::async {

LCF_ASYNC_EXPORT
using Executor = asio::any_io_executor;

}
