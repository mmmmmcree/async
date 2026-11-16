#pragma once

#include <async/core/queue/utils/pop_one.h>
#include <async/core/stream/declarations/concepts.h>

#include <ranges>
#include <type_traits>

LCF_ASYNC_EXPORT
namespace lcf::async::utils {

template <drainable_source_c Source>
requires (not std::is_pointer_v<std::remove_reference_t<Source>>)
source_value_t<Source> * pull_any(Source & source) noexcept
{
    if constexpr (consumer_queue_c<Source>) { return queue::utils::pop_one(source); }
    else { return source.pullAny(); }
}

template <drainable_source_c Source>
requires (not std::is_pointer_v<Source>)
source_value_t<Source> * pull_any(Source * source_p) noexcept { return pull_any(*source_p); }

template <drainable_source_range_c Sources>
source_value_t<std::ranges::range_reference_t<Sources>> * pull_any(Sources && sources) noexcept
{
    for (auto && source : sources) {
        if (auto * value_p = pull_any(source)) { return value_p; }
    }
    return nullptr;
}

} // namespace lcf::async::utils
