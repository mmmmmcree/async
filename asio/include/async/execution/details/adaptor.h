#pragma once

#include <async/core/protocol.h>
#include <functional>

namespace lcf::async::exec::details {

template <typename Function>
struct Adaptor
{
    template <sender Sender>
        requires std::invocable<Function &&, Sender>
    auto operator()(Sender && sender) && { return std::move(m_function)(std::forward<Sender>(sender)); }
    template <sender Sender>
        requires std::copy_constructible<Function> and std::invocable<Function &&, Sender>
    auto operator()(Sender && sender) const & { auto copy = *this; return std::move(copy)(std::forward<Sender>(sender)); }
    template <sender Sender>
        requires std::invocable<Function &&, Sender>
    friend auto operator|(Sender && sender, Adaptor adaptor) { return std::move(adaptor)(std::forward<Sender>(sender)); }
    Function m_function;
};

}
