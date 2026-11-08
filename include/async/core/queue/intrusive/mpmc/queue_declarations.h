#pragma once

#include "AlgorithmTag.h"
#include <async/core/queue/declarations/concepts.h>

LCF_ASYNC_EXPORT
namespace lcf::async::queue::intrusive::mpmc {

template <linked_node_c Node, AlgorithmTag algorithm = AlgorithmTag::eMutex>
class Queue;

} // namespace lcf::async::queue::intrusive::mpmc
