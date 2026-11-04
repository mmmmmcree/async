#pragma once

#include <async/core/worker/thread/declarations/aliases.h>

LCF_ASYNC_EXPORT
namespace lcf::async {

struct NullThreadHandle
{
    ~NullThreadHandle() noexcept = default;
    NullThreadHandle() noexcept = default;
    NullThreadHandle(ThreadEntry, void *) noexcept {}
    NullThreadHandle(const NullThreadHandle &) noexcept = default;
    NullThreadHandle(NullThreadHandle &&) noexcept = default;
    NullThreadHandle & operator=(const NullThreadHandle &) noexcept = default;
    NullThreadHandle & operator=(NullThreadHandle &&) noexcept = default;
public:
    bool joinable() const noexcept { return false; }
    void join() noexcept {}
};

} // namespace lcf::async
