#pragma once

#include <async/config.h>
#include <utility>
#include <async/core/coroutine/details/frame.h>

namespace lcf::async {

// Selects a resource for synchronous coroutine creation on this thread, before suspension.
// The resource must outlive those frames and support their eventual deallocation threads.
LCF_ASYNC_EXPORT
class FrameAllocatorScope
{
    using Self = FrameAllocatorScope;
public:
    ~FrameAllocatorScope() noexcept { details::current_frame_resource() = m_previous_p; }
    explicit FrameAllocatorScope(std::pmr::memory_resource & resource) noexcept :
        m_previous_p(std::exchange(details::current_frame_resource(), &resource)) {}
    FrameAllocatorScope(const Self &) = delete;
    FrameAllocatorScope(Self &&) = delete;
    Self & operator=(const Self &) = delete;
    Self & operator=(Self &&) = delete;
private:
    std::pmr::memory_resource * m_previous_p;
};

}
