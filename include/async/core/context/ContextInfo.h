#pragma once

#include <async/config/macros.h>

#include <cstdint>

LCF_ASYNC_EXPORT
namespace lcf::async {

using WorkerCount = std::uint32_t;

struct StorageRequirement;
struct ContextStorageRequirements;
struct ContextAllocators;
class ContextInfo;

} // namespace lcf::async
