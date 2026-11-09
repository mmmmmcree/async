#pragma once

#include <async/core/worker/thread/declarations/aliases.h>
#include <async/core/worker/thread/declarations/concepts.h>

#include <concepts>
#include <type_traits>

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
        noexcept(std::is_nothrow_constructible_v<ThreadHandle, ThreadEntry, void *>)
    requires std::constructible_from<ThreadHandle, ThreadEntry, void *>
    {
        return ThreadHandle{entry_f, state_p};
    }
};

} // namespace lcf::async
