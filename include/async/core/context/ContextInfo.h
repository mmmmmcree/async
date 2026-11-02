#pragma once

#include <async/config/macros.h>

#include <cstdint>

LCF_ASYNC_EXPORT
namespace lcf::async {

using WorkerCount = std::uint32_t;

enum class QueueTarget;

struct QueueBinding;
struct StorageRequirement;
struct ContextStorageRequirements;
struct ContextAllocators;
class ContextInfo;

} // namespace lcf::async
