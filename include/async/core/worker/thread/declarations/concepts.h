#pragma once

#include "aliases.h"

#include <concepts>

LCF_ASYNC_EXPORT
namespace lcf::async {

template <typename Handle>
concept join_handle_c = std::destructible<Handle> and
    requires(Handle & handle, const Handle & const_handle) {
        { const_handle.joinable() } noexcept -> std::same_as<bool>;
        { handle.join() } -> std::same_as<void>;
    };

template <typename Launcher, typename ThreadHandle>
concept thread_launcher_c = join_handle_c<ThreadHandle> and
    requires(Launcher & launcher, ThreadEntry entry_f, void * state_p) {
        { launcher(entry_f, state_p) } -> std::same_as<ThreadHandle>;
    };

} // namespace lcf::async
