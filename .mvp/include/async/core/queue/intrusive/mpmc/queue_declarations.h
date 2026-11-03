#pragma once

#include "AlgorithmTag.h"

namespace lcf::async::queue::intrusive::mpmc {

template <AlgorithmTag Algorithm = AlgorithmTag::eMutex>
class Queue;

} // namespace lcf::async::queue::intrusive::mpmc
