#pragma once

#include <async/config/macros.h>

#include "AlgorithmTag.h"

LCF_ASYNC_EXPORT
namespace lcf::async::queue::intrusive::spmc {

template <AlgorithmTag Algorithm = AlgorithmTag::eExchangeLinked>
class Queue;

} // namespace lcf::async::queue::intrusive::spmc
