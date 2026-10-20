#pragma once

#include <async/config.h>

#include <asio/io_context.hpp>

namespace lcf::async {

LCF_ASYNC_EXPORT
using Context = asio::io_context;

}
