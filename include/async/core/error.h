#pragma once

#include <async/config/macros.h>

#include <system_error>

LCF_ASYNC_EXPORT
namespace lcf::async {

enum class errc
{
    unexpected_exception = 1,
    service_registration_closed,
};

namespace details {

class AsyncErrorCategory final : public std::error_category
{
public:
    ~AsyncErrorCategory() noexcept override = default;
    AsyncErrorCategory() noexcept = default;
    AsyncErrorCategory(const AsyncErrorCategory &) = delete;
    AsyncErrorCategory(AsyncErrorCategory &&) = delete;
    AsyncErrorCategory & operator=(const AsyncErrorCategory &) = delete;
    AsyncErrorCategory & operator=(AsyncErrorCategory &&) = delete;
    const char * name() const noexcept override { return "lcf.async"; }
    std::string message(int value) const override
    {
        switch (static_cast<errc>(value)) {
        case errc::unexpected_exception: return "unexpected exception";
        case errc::service_registration_closed: return "service registration is closed";
        }
        return "unknown async error";
    }
};

} // namespace details

inline const std::error_category & error_category() noexcept
{
    static const details::AsyncErrorCategory category;
    return category;
}

inline std::error_code make_error_code(errc value) noexcept
{
    return {static_cast<int>(value), error_category()};
}

} // namespace lcf::async

namespace std {

template <>
struct is_error_code_enum<lcf::async::errc> : true_type {};

} // namespace std
