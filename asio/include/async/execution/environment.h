#pragma once

#include <async/core/protocol/details/utility.h>

namespace lcf::async::exec::details {

template <typename Receiver, typename Environment>
struct EnvironmentReceiver
{
    using receiver_concept = receiver_t;
    Environment get_env() const noexcept { return m_environment; }
    template <typename... Values>
    void set_value(Values &&... values) noexcept { exec::set_value(std::move(m_receiver), std::forward<Values>(values)...); }
    template <typename Error>
    void set_error(Error && error) noexcept { exec::set_error(std::move(m_receiver), std::forward<Error>(error)); }
    void set_stopped() noexcept { exec::set_stopped(std::move(m_receiver)); }
    Receiver m_receiver;
    Environment m_environment;
};
template <typename Source, typename Query, typename Value>
struct EnvironmentSender
{
    using sender_concept = sender_t;
    using completion_signatures = completion_signatures_of_t<Source>;
    auto get_env() const noexcept { return exec::get_env(m_source); }
    template <receiver Receiver>
    auto connect(Receiver receiver) &&
    {
        using Environment = EnvironmentOverride<env_of_t<Receiver>, Query, Value>;
        Environment environment {exec::get_env(receiver), std::move(m_value)};
        return exec::connect(std::move(m_source), EnvironmentReceiver<Receiver, Environment> {std::move(receiver), std::move(environment)});
    }
    Source m_source;
    Value m_value;
};

}

namespace lcf::async::exec {

LCF_ASYNC_EXPORT
template <sender Source>
auto with_stop_token(Source && source, std::stop_token token)
{ return details::EnvironmentSender<std::decay_t<Source>, get_stop_token_t, std::stop_token> {std::forward<Source>(source), token}; }
LCF_ASYNC_EXPORT
template <sender Source>
auto with_allocator(Source && source, std::pmr::memory_resource * resource_p)
{
    return details::EnvironmentSender<std::decay_t<Source>, get_allocator_t, std::pmr::polymorphic_allocator<std::byte>> {
        std::forward<Source>(source), std::pmr::polymorphic_allocator<std::byte> {resource_p}};
}

}
