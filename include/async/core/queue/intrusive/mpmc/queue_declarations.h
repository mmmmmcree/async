#pragma once

#include <async/config/macros.h>

#include "AlgorithmTag.h"

LCF_ASYNC_EXPORT
namespace lcf::async::queue::intrusive::mpmc {

template <AlgorithmTag Algorithm = AlgorithmTag::eMutex>
class Queue;

} // namespace lcf::async::queue::intrusive::mpmc
