#pragma once

namespace lcf::async {

LCF_ASYNC_HEADER_INLINE
Awaitable<void> yield()
{
    co_await details::YieldAwaitable {};
}

}
