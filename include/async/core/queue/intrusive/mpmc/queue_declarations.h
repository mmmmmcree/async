#pragma once

#include "AlgorithmTag.h"
#include <async/core/queue/ExecutionNode.h>

LCF_ASYNC_EXPORT
namespace lcf::async::queue::intrusive::mpmc {

template <AlgorithmTag algorithm = AlgorithmTag::eMutex, typename Node = queue::ExecutionNode>
class Queue;

} // namespace lcf::async::queue::intrusive::mpmc
