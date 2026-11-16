#pragma once

#include <async/core/policy/declarations/concepts.h>
#include <async/core/scheduling/declarations/enums.h>
#include <async/core/stream/SelectedStream.h>
#include <async/core/stream/declarations/concepts.h>
#include <async/core/utils/drain.h>

#include <span>
#include <utility>

LCF_ASYNC_EXPORT
namespace lcf::async {

template <stream_c Stream, typename SelectionPolicy>
requires selection_policy_c<SelectionPolicy, std::span<Stream>>
class PriorityStream
{
public:
    using value_t = typename Stream::value_t;
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
    value_t * pull() noexcept { return m_selected.pull(); }
    bool isEmpty() const noexcept { return m_selected.isEmpty(); }
    void requeue(value_t & record) noexcept requires retry_stream_c<Stream>
    {
        m_priorities[static_cast<std::size_t>(record.getPriority())].requeue(record);
    }
    value_t * pullRemaining() noexcept requires drainable_stream_c<Stream>
    {
        for (Stream & stream : m_priorities) {
            if (value_t * value_p = stream.pullRemaining()) { return value_p; }
        }
        return nullptr;
    }
    auto drain() noexcept requires drainable_stream_c<Stream> { return utils::drain(*this); }
private:
    std::span<Stream> m_priorities;
    SelectedStream<Stream, SelectionPolicy> m_selected;
};

} // namespace lcf::async
