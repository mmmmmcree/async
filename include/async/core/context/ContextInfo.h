#pragma once

#include <async/config/macros.h>

#include <cstdint>

LCF_ASYNC_EXPORT
namespace lcf::async {

using WorkerIndex = std::uint32_t;

struct QueueBinding;
struct StorageRequirement;
struct ContextStorageRequirements;
struct ContextAllocators;
class ContextInfo;

} // namespace lcf::async
