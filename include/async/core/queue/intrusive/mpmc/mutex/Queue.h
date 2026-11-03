#pragma once

#include <async/config/macros.h>

#include <async/core/queue/intrusive/mpmc/queue_declarations.h>

LCF_ASYNC_EXPORT
namespace lcf::async::queue::intrusive::mpmc {

template <>
class Queue<AlgorithmTag::eMutex>;

} // namespace lcf::async::queue::intrusive::mpmc
