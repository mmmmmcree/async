#pragma once

#include <async/core/policy/declarations/concepts.h>
#include <async/core/policy/utils/selected.h>
#include <async/core/stream/declarations/concepts.h>
#include <async/core/stream/utils/pull_one.h>
#include <async/core/stream/utils/pull_any.h>
#include <async/core/stream/utils/pull_all.h>

#include <algorithm>
#include <span>
#include <type_traits>
#include <utility>

LCF_ASYNC_EXPORT
namespace lcf::async {

template <stream_source_c Source, typename SelectionPolicy>
requires selection_policy_c<SelectionPolicy, std::span<Source>>
class SelectedStream
{
public:
    using value_t = source_value_t<Source>;
public:
    ~SelectedStream() noexcept = default;
    SelectedStream() = delete;
    explicit SelectedStream(std::span<Source> sources, SelectionPolicy policy = {}) :
        m_sources(sources), m_policy(std::move(policy)) {}
    SelectedStream(const SelectedStream &) = default;
    SelectedStream(SelectedStream &&) = default;
    SelectedStream & operator=(const SelectedStream &) = default;
    SelectedStream & operator=(SelectedStream &&) = default;
public:
    value_t * pullOne() noexcept { return utils::pull_one(m_sources | utils::selected(m_policy)); }
    value_t * pullAny() noexcept requires drainable_source_c<Source> { return utils::pull_any(m_sources); }
    auto pullAll() noexcept requires drainable_source_c<Source> { return utils::pull_all(*this); }
    bool isEmpty() const noexcept
    {
        return std::ranges::all_of(m_sources, [](const Source & source) {
            if constexpr (std::is_pointer_v<Source>) { return source->isEmpty(); }
            else { return source.isEmpty(); }
        });
    }
private:
    std::span<Source> m_sources;
    SelectionPolicy m_policy;
};

} // namespace lcf::async
