#pragma once

#include <async/core/policy/declarations/concepts.h>
#include <async/core/scheduling/declarations/enums.h>
#include <async/core/stream/SelectedStream.h>
#include <async/core/stream/declarations/concepts.h>
#include <async/core/stream/utils/pull_any.h>
#include <async/core/stream/utils/pull_one.h>
#include <async/core/stream/utils/pull_all.h>

#include <span>
#include <type_traits>
#include <utility>

LCF_ASYNC_EXPORT
namespace lcf::async {

template <stream_c Stream, typename SelectionPolicy>
requires selection_policy_c<SelectionPolicy, std::span<Stream>>
class PriorityStream
{
public:
    using value_t = source_value_t<Stream>;
public:
    ~PriorityStream() noexcept = default;
    PriorityStream() = delete;
    explicit PriorityStream(std::span<Stream> priorities, SelectionPolicy policy = {}) :
        m_priorities(priorities), m_selected(priorities, std::move(policy)) {}
    PriorityStream(const PriorityStream &) = default;
    PriorityStream(PriorityStream &&) = default;
    PriorityStream & operator=(const PriorityStream &) = default;
    PriorityStream & operator=(PriorityStream &&) = default;
public:
    value_t * pullOne() noexcept { return utils::pull_one(m_selected); }
    value_t * pullAny() noexcept requires drainable_stream_c<Stream> { return utils::pull_any(m_priorities); }
    auto pullAll() noexcept requires drainable_stream_c<Stream> { return utils::pull_all(*this); }
    bool isEmpty() const noexcept { return m_selected.isEmpty(); }
    void requeue(value_t & record) noexcept requires retry_stream_c<Stream>
    {
        auto & stream = m_priorities[static_cast<std::size_t>(record.getPriority())];
        if constexpr (std::is_pointer_v<Stream>) { stream->requeue(record); }
        else { stream.requeue(record); }
    }
private:
    std::span<Stream> m_priorities;
    SelectedStream<Stream, SelectionPolicy> m_selected;
};

} // namespace lcf::async
