#pragma once

#include <async/core/driver/declarations/structs.h>
#include <async/core/stream/declarations/concepts.h>

#include <concepts>

LCF_ASYNC_EXPORT
namespace lcf::async {

template <typename Driver>
concept driver_c = requires(Driver & driver) {
    { driver.driveOne() } noexcept -> std::same_as<DriveReport>;
};

} // namespace lcf::async
