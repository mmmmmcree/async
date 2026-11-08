#pragma once

#include <async/config/macros.h>

LCF_ASYNC_EXPORT
namespace lcf::async::notify {

class NoopNotifier
{
public:
    ~NoopNotifier() noexcept = default;
    NoopNotifier() noexcept = default;
    NoopNotifier(const NoopNotifier &) noexcept = default;
    NoopNotifier(NoopNotifier &&) noexcept = default;
    NoopNotifier & operator=(const NoopNotifier &) noexcept = default;
    NoopNotifier & operator=(NoopNotifier &&) noexcept = default;
public:
    void notifyOne() const noexcept {}
    void notifyAll() const noexcept {}
};

} // namespace lcf::async::notify
