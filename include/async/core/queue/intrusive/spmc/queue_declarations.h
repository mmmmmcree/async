#pragma once

#include "AlgorithmTag.h"
#include <async/core/queue/declarations/concepts.h>

LCF_ASYNC_EXPORT
namespace lcf::async::queue::intrusive::spmc {

template <linked_node_c Node, AlgorithmTag algorithm = AlgorithmTag::eExchangeLinked>
class Queue;

} // namespace lcf::async::queue::intrusive::spmc
