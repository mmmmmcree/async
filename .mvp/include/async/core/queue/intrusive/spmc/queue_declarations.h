#pragma once

#include "AlgorithmTag.h"

namespace lcf::async::queue::intrusive::spmc {

template <AlgorithmTag Algorithm = AlgorithmTag::eExchangeLinked>
class Queue;

} // namespace lcf::async::queue::intrusive::spmc
