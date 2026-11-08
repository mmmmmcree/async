#pragma once

#include <async/config/macros.h>

#include <concepts>
#include <type_traits>

LCF_ASYNC_EXPORT
namespace lcf::async::notify {

template <typename Waiter>
concept signal_waiter_c = requires {
    typename Waiter::Observation;
    requires std::copy_constructible<typename Waiter::Observation>;
} and requires(Waiter & waiter, typename Waiter::Observation observed) {
    { waiter.observe() } noexcept -> std::same_as<typename Waiter::Observation>;
    { waiter.wait(observed) } noexcept -> std::same_as<void>;
};

template <typename Notifier>
concept notifier_c = requires(Notifier & notifier) {
    { notifier.notifyOne() } noexcept -> std::same_as<void>;
    { notifier.notifyAll() } noexcept -> std::same_as<void>;
};

template <typename Signal>
concept signal_c = signal_waiter_c<Signal> and notifier_c<Signal>;

template <typename Notifier, typename Signal>
concept signal_notifier_c = signal_c<Signal> and notifier_c<Notifier>
    and std::is_nothrow_constructible_v<Notifier, Signal &>
    and std::copy_constructible<Notifier>;

} // namespace lcf::async::notify
