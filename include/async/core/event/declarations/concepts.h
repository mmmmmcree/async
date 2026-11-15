#pragma once
#include <async/config/macros.h>
#include <async/core/driver/declarations/concepts.h>
#include <chrono>
LCF_ASYNC_EXPORT
namespace lcf::async {
template <typename Backend>
concept event_backend_c = driver_c<Backend> and requires(Backend & backend, std::chrono::steady_clock::time_point deadline) {
    { backend.waitUntil(deadline) } noexcept -> std::same_as<std::error_code>;
    { backend.wake() } noexcept -> std::same_as<void>;
};
}
