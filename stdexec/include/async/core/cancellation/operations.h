#pragma once

#include <async/config.h>
#include <functional>
#include <stop_token>
#include <utility>

namespace lcf::async {

LCF_ASYNC_EXPORT
template <typename Completion>
class StopBoundCompletion
{
public:
    StopBoundCompletion(std::stop_token token, Completion completion) : m_token(token), m_completion(std::move(completion)) {}
    std::stop_token getStopToken() const noexcept { return m_token; }
    template <typename... Arguments>
    decltype(auto) operator()(Arguments &&... arguments) { return std::invoke(m_completion, std::forward<Arguments>(arguments)...); }
private:
    std::stop_token m_token;
    Completion m_completion;
};

LCF_ASYNC_EXPORT
template <typename Completion>
auto bind_stop_token(std::stop_token token, Completion completion) { return StopBoundCompletion<Completion> {token, std::move(completion)}; }

}

namespace lcf::async {

LCF_ASYNC_EXPORT
using CancellationSource = std::stop_source;

LCF_ASYNC_EXPORT
template <typename Completion>
auto bind_cancellation(CancellationSource & source, Completion completion)
{
    return bind_stop_token(source.get_token(), std::move(completion));
}

LCF_ASYNC_EXPORT
inline void request_cancellation(CancellationSource & source) noexcept { source.request_stop(); }

}
