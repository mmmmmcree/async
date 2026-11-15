#pragma once
#include <async/config/macros.h>
#include <async/core/service/ServiceHandle.h>
#include <async/core/driver/declarations/concepts.h>
LCF_ASYNC_EXPORT
namespace lcf::async {
template <typename Service>
concept service_c = driver_c<Service> and requires(Service & service, ServiceHandle handle) {
    { service.start(handle) } noexcept -> std::same_as<std::error_code>;
    { service.requestStop() } noexcept -> std::same_as<void>;
};
}
