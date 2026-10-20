#pragma once

#include <async/config.h>
#include <concepts>
#include <exception>
#include <memory_resource>
#include <stop_token>
#include <type_traits>
#include <utility>

namespace lcf::async::exec {

LCF_ASYNC_EXPORT
struct sender_t {};
LCF_ASYNC_EXPORT
struct receiver_t {};
LCF_ASYNC_EXPORT
struct operation_state_t {};

LCF_ASYNC_EXPORT
struct set_value_t
{
    template <typename Receiver, typename... Values>
    void operator()(Receiver && receiver, Values &&... values) const noexcept
    {
        static_assert(noexcept(std::forward<Receiver>(receiver).set_value(std::forward<Values>(values)...)));
        std::forward<Receiver>(receiver).set_value(std::forward<Values>(values)...);
    }
};
LCF_ASYNC_EXPORT
struct set_error_t
{
    template <typename Receiver, typename Error>
    void operator()(Receiver && receiver, Error && error) const noexcept
    {
        static_assert(noexcept(std::forward<Receiver>(receiver).set_error(std::forward<Error>(error))));
        std::forward<Receiver>(receiver).set_error(std::forward<Error>(error));
    }
};
LCF_ASYNC_EXPORT
struct set_stopped_t
{
    template <typename Receiver>
    void operator()(Receiver && receiver) const noexcept
    {
        static_assert(noexcept(std::forward<Receiver>(receiver).set_stopped()));
        std::forward<Receiver>(receiver).set_stopped();
    }
};
LCF_ASYNC_EXPORT
inline constexpr set_value_t set_value {};
LCF_ASYNC_EXPORT
inline constexpr set_error_t set_error {};
LCF_ASYNC_EXPORT
inline constexpr set_stopped_t set_stopped {};

LCF_ASYNC_EXPORT
struct empty_env {};
LCF_ASYNC_EXPORT
struct get_env_t
{
    template <typename Object>
    auto operator()(const Object & object) const noexcept
    {
        if constexpr (requires { object.get_env(); }) {
            static_assert(noexcept(object.get_env()));
            return object.get_env();
        } else { return empty_env {}; }
    }
};
LCF_ASYNC_EXPORT
inline constexpr get_env_t get_env {};

LCF_ASYNC_EXPORT
struct get_stop_token_t
{
    template <typename Environment>
    auto operator()(const Environment & environment) const noexcept
    {
        if constexpr (requires { environment.query(*this); }) { return environment.query(*this); }
        else { return std::stop_token {}; }
    }
};
LCF_ASYNC_EXPORT
struct get_allocator_t
{
    template <typename Environment>
    auto operator()(const Environment & environment) const noexcept
    {
        if constexpr (requires { environment.query(*this); }) { return environment.query(*this); }
        else { return std::pmr::polymorphic_allocator<std::byte> {}; }
    }
};
LCF_ASYNC_EXPORT
struct get_scheduler_t
{
    template <typename Environment>
        requires requires(const Environment & environment, get_scheduler_t query) { environment.query(query); }
    auto operator()(const Environment & environment) const noexcept { return environment.query(*this); }
};
LCF_ASYNC_EXPORT
template <typename Completion>
struct get_completion_scheduler_t
{
    template <typename Environment>
        requires requires(const Environment & environment, get_completion_scheduler_t query) { environment.query(query); }
    auto operator()(const Environment & environment) const noexcept { return environment.query(*this); }
};
LCF_ASYNC_EXPORT
inline constexpr get_stop_token_t get_stop_token {};
LCF_ASYNC_EXPORT
inline constexpr get_allocator_t get_allocator {};
LCF_ASYNC_EXPORT
inline constexpr get_scheduler_t get_scheduler {};
LCF_ASYNC_EXPORT
template <typename Completion>
inline constexpr get_completion_scheduler_t<Completion> get_completion_scheduler {};
LCF_ASYNC_EXPORT
template <typename Query, typename Value>
struct prop
{
    Value query(Query) const noexcept(std::is_nothrow_copy_constructible_v<Value>) { return m_value; }
    Query m_query;
    Value m_value;
};

LCF_ASYNC_EXPORT
template <typename... Signatures>
struct completion_signatures {};
LCF_ASYNC_EXPORT
template <typename Sender, typename Environment = empty_env>
using completion_signatures_of_t = typename std::remove_cvref_t<Sender>::completion_signatures;
LCF_ASYNC_EXPORT
template <typename Receiver>
using env_of_t = decltype(get_env(std::declval<Receiver>()));

LCF_ASYNC_EXPORT
template <typename Sender>
concept sender = std::move_constructible<std::remove_cvref_t<Sender>>
    and requires { typename std::remove_cvref_t<Sender>::sender_concept; typename completion_signatures_of_t<Sender>; };
LCF_ASYNC_EXPORT
template <typename Receiver>
concept receiver = std::move_constructible<std::remove_cvref_t<Receiver>>
    and requires { typename std::remove_cvref_t<Receiver>::receiver_concept; };
LCF_ASYNC_EXPORT
template <typename Operation>
concept operation_state = requires(Operation & operation) { { operation.start() } noexcept -> std::same_as<void>; };

LCF_ASYNC_EXPORT
struct connect_t
{
    template <sender Sender, receiver Receiver>
        requires requires(Sender && source, Receiver destination) { std::forward<Sender>(source).connect(std::move(destination)); }
    auto operator()(Sender && source, Receiver destination) const
        noexcept(noexcept(std::forward<Sender>(source).connect(std::move(destination))))
    { return std::forward<Sender>(source).connect(std::move(destination)); }
};
LCF_ASYNC_EXPORT
struct start_t
{
    template <operation_state Operation>
    void operator()(Operation & operation) const noexcept { operation.start(); }
};
LCF_ASYNC_EXPORT
struct schedule_t
{
    template <typename Scheduler>
    auto operator()(Scheduler && scheduler) const noexcept(noexcept(std::forward<Scheduler>(scheduler).schedule()))
    { return std::forward<Scheduler>(scheduler).schedule(); }
};
LCF_ASYNC_EXPORT
inline constexpr connect_t connect {};
LCF_ASYNC_EXPORT
inline constexpr start_t start {};
LCF_ASYNC_EXPORT
inline constexpr schedule_t schedule {};
LCF_ASYNC_EXPORT
template <typename Sender, typename Receiver>
using connect_result_t = decltype(connect(std::declval<Sender>(), std::declval<Receiver>()));
LCF_ASYNC_EXPORT
template <typename Sender, typename Receiver>
concept sender_to = sender<Sender> and receiver<Receiver> and requires { typename connect_result_t<Sender, Receiver>; };
LCF_ASYNC_EXPORT
template <typename Scheduler>
concept scheduler = std::copy_constructible<Scheduler> and std::equality_comparable<Scheduler>
    and requires(Scheduler value) { { schedule(value) } -> sender; };

}
