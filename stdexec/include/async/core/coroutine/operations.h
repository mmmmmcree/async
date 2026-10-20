#pragma once

#include <async/dependencies.h>

#include <async/core/coroutine/Awaitable.h>
#include <async/core/scheduling/Scheduler.h>

namespace lcf::async {

namespace details {

class YieldAwaitable
{
    struct Environment
    {
        static constexpr auto query(::exec::get_completion_behavior_t<stde::set_value_t>) noexcept
        {
            return ::exec::completion_behavior::asynchronous_affine;
        }
    };
public:
    using sender_concept = stde::sender_t;
    using completion_signatures = stde::completion_signatures<stde::set_value_t(), stde::set_error_t(std::exception_ptr), stde::set_stopped_t()>;
public:
    Environment get_env() const noexcept { return {}; }
    template <typename Promise>
    requires requires (Promise & promise) { stde::get_start_scheduler(stde::get_env(promise)); }
    auto as_awaitable(Promise & promise) const
    {
        return stde::as_awaitable(stde::schedule(stde::get_start_scheduler(stde::get_env(promise))), promise);
    }
};

}

LCF_ASYNC_EXPORT
LCF_ASYNC_HEADER_INLINE
Awaitable<void> yield();

}

#ifndef LCF_ASYNC_BUILDING_INTERFACE
#include <async/core/coroutine/details/yield.h>
#endif

namespace lcf::async {

LCF_ASYNC_EXPORT
template <stde::sender Sender>
auto as_sender(Scheduler scheduler, Sender work)
{ return stde::starts_on(std::move(scheduler), std::move(work)); }

}
