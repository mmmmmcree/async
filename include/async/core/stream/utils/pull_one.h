#pragma once

#include <async/core/queue/utils/pop_one.h>
#include <async/core/stream/declarations/concepts.h>

#include <ranges>
#include <type_traits>

LCF_ASYNC_EXPORT
namespace lcf::async::utils {

template <stream_source_c Source>
requires (not std::is_pointer_v<std::remove_reference_t<Source>>)
source_value_t<Source> * pull_one(Source & source) noexcept
{
    if constexpr (stream_c<Source>) { return source.pullOne(); }
    else { return queue::utils::pop_one(source); }
}

template <stream_source_c Source>
requires (not std::is_pointer_v<Source>)
source_value_t<Source> * pull_one(Source * source_p) noexcept { return pull_one(*source_p); }

template <stream_source_range_c Sources>
source_value_t<std::ranges::range_reference_t<Sources>> * pull_one(Sources && sources) noexcept
{
    for (auto && source : sources) {
        if (auto * value_p = pull_one(source)) { return value_p; }
    }
    return nullptr;
}

} // namespace lcf::async::utils
