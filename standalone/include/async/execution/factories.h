#pragma once

#include <async/core/protocol/details/utility.h>

namespace lcf::async::exec::details {

template <typename Tag, typename... Values>
struct ImmediateSender
{
    using sender_concept = sender_t;
    using completion_signatures = exec::completion_signatures<Tag(Values...)>;
    template <typename Receiver>
    struct Operation
    {
        using operation_state_concept = operation_state_t;
        void start() noexcept
        { std::apply([&](auto &... values) noexcept { Tag {}(std::move(m_receiver), std::move(values)...); }, m_values); }
        Receiver m_receiver;
        std::tuple<Values...> m_values;
    };
    template <receiver Receiver>
    auto connect(Receiver receiver) && { return Operation<Receiver> {std::move(receiver), std::move(m_values)}; }
    template <receiver Receiver>
        requires (std::copy_constructible<Values> and ...)
    auto connect(Receiver receiver) const & { return Operation<Receiver> {std::move(receiver), m_values}; }
    std::tuple<Values...> m_values;
};

}

namespace lcf::async::exec {

LCF_ASYNC_EXPORT
template <typename... Values>
auto just(Values &&... values) { return details::ImmediateSender<set_value_t, std::decay_t<Values>...> {{std::forward<Values>(values)...}}; }
LCF_ASYNC_EXPORT
template <typename Error>
auto just_error(Error && error) { return details::ImmediateSender<set_error_t, std::decay_t<Error>> {{std::forward<Error>(error)}}; }
LCF_ASYNC_EXPORT
inline auto just_stopped() noexcept { return details::ImmediateSender<set_stopped_t> {}; }

}
