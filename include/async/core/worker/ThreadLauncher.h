#pragma once

#include <async/core/worker/declarations/concepts.h>

#include <concepts>

LCF_ASYNC_EXPORT
namespace lcf::async {

template <join_handle_c ThreadHandle>
struct ThreadLauncher
{
    ~ThreadLauncher() noexcept = default;
    ThreadLauncher() noexcept = default;
    ThreadLauncher(const ThreadLauncher &) noexcept = default;
    ThreadLauncher(ThreadLauncher &&) noexcept = default;
    ThreadLauncher & operator=(const ThreadLauncher &) noexcept = default;
    ThreadLauncher & operator=(ThreadLauncher &&) noexcept = default;
public:
    ThreadHandle operator()(ThreadEntry entry_f, void * state_p) const
    requires std::constructible_from<ThreadHandle, ThreadEntry, void *> { return ThreadHandle{entry_f, state_p}; }
};

} // namespace lcf::async
