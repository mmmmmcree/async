#pragma once

#include "AlgorithmTag.h"

LCF_ASYNC_EXPORT
namespace lcf::async::queue::intrusive::spmc {

template <AlgorithmTag algorithm = AlgorithmTag::eExchangeLinked>
class Queue;

} // namespace lcf::async::queue::intrusive::spmc
