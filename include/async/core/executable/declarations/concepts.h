#pragma once

#include <async/config/macros.h>
#include <async/core/details/concepts/constructible.h>

#include <concepts>

LCF_ASYNC_EXPORT
namespace lcf::async {

template <typename Executable>
concept executable_c = requires(Executable & executable) {
    { executable.execute() } noexcept -> std::same_as<void>;
};

template <typename Executable>
concept cancellable_executable_c = executable_c<Executable> and requires(Executable & executable) {
    { executable.cancel() } noexcept -> std::same_as<void>;
};

template <typename Executable>
concept callback_executable_c = cancellable_executable_c<Executable> and
    details::nothrow_constructible_from_c<Executable, void *, void (*)(void *) noexcept, void (*)(void *) noexcept>;

} // namespace lcf::async
