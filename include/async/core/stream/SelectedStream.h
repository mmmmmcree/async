#pragma once

#include <async/core/queue/utils/pop_one.h>
#include <async/core/policy/declarations/concepts.h>
#include <async/core/policy/utils/selected.h>
#include <async/core/stream/declarations/concepts.h>
#include <async/core/utils/drain.h>

#include <algorithm>
#include <span>
#include <utility>

LCF_ASYNC_EXPORT
namespace lcf::async {

template <stream_source_c Source, typename SelectionPolicy>
requires selection_policy_c<SelectionPolicy, std::span<Source>>
class SelectedStream
{
public:
    using value_t = typename Source::value_t;
public:
    ~SelectedStream() noexcept = default;
    SelectedStream() = delete;
    explicit SelectedStream(std::span<Source> sources, SelectionPolicy policy = {}) :
        m_sources(sources), m_policy(std::move(policy)) {}
    SelectedStream(const SelectedStream &) = default;
    SelectedStream(SelectedStream &&) = default;
    SelectedStream & operator=(const SelectedStream &) = default;
    SelectedStream & operator=(SelectedStream &&) = default;
    value_t * pull() noexcept
    {
        for (Source & source : m_policy.select(std::views::all(m_sources))) {
            value_t * value_p;
            if constexpr (stream_c<Source>) { value_p = source.pull(); }
            else { value_p = queue::utils::pop_one(source); }
            if (value_p) { return value_p; }
        }
        return nullptr;
    }
    bool isEmpty() const noexcept
    {
        return std::ranges::all_of(m_sources, [](const Source & source) { return source.isEmpty(); });
    }
    value_t * pullRemaining() noexcept requires drainable_source_c<Source>
    {
        for (Source & source : m_sources) {
            value_t * value_p;
            if constexpr (consumer_queue_c<Source>) { value_p = queue::utils::pop_one(source); }
            else { value_p = source.pullRemaining(); }
            if (value_p) { return value_p; }
        }
        return nullptr;
    }
    auto drain() noexcept requires drainable_source_c<Source> { return utils::drain(*this); }
private:
    std::span<Source> m_sources;
    SelectionPolicy m_policy;
};

} // namespace lcf::async
