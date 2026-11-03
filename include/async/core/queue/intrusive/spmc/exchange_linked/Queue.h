#pragma once

#include <async/config/macros.h>

#include <async/core/queue/intrusive/spmc/queue_declarations.h>

LCF_ASYNC_EXPORT
namespace lcf::async::queue::intrusive::spmc {

template <>
class Queue<AlgorithmTag::eExchangeLinked>;

} // namespace lcf::async::queue::intrusive::spmc
