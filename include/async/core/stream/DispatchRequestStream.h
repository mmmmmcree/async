#pragma once

#include <async/config/macros.h>
#include <async/core/queue/intrusive/Queue.h>
#include <async/core/policy/utils/selected.h>
#include <async/core/stream/declarations/concepts.h>
#include <async/core/utils/drain.h>

#include <algorithm>
#include <array>
#include <span>
#include <ranges>

LCF_ASYNC_EXPORT
namespace lcf::async {

template <consumer_queue_c Queue, typename InputSelectionPolicy, typename RequestSelectionPolicy>
class DispatchRequestStream
{
    enum class Source { eIncoming, ePending };
    using Drain = utils::details::DrainView<DispatchRequestStream>;
public:
    using value_t = typename Queue::value_t;
public:
    ~DispatchRequestStream() noexcept = default;
    DispatchRequestStream() = delete;
    explicit DispatchRequestStream(std::span<Queue *> queues, InputSelectionPolicy input_policy = {},
        RequestSelectionPolicy request_policy = {}) : m_queues(queues),
        m_input_policy(std::move(input_policy)), m_request_policy(std::move(request_policy)) {}
    DispatchRequestStream(const DispatchRequestStream &) = delete;
    DispatchRequestStream(DispatchRequestStream &&) = delete;
    DispatchRequestStream & operator=(const DispatchRequestStream &) = delete;
    DispatchRequestStream & operator=(DispatchRequestStream &&) = delete;
public:
    value_t * pull() noexcept
    {
        for (const Source source : m_request_policy.select(std::views::all(m_sources))) {
            if (source == Source::ePending) {
                if (value_t * record_p = m_pending.pop()) { return record_p; }
            } else {
                for (Queue * queue_p : m_input_policy.select(std::views::all(m_queues))) {
                    if (value_t * record_p = queue_p->pop()) { return record_p; }
                }
            }
        }
        return nullptr;
    }
    void requeue(value_t & record) noexcept { m_pending.pushBack(record); }
    bool isEmpty() const noexcept
    {
        return m_pending.isEmpty() and std::ranges::all_of(m_queues, [](Queue * queue_p) { return queue_p->isEmpty(); });
    }
    value_t * pullRemaining() noexcept
    {
        if (value_t * record_p = m_pending.pop()) { return record_p; }
        for (Queue * queue_p : m_queues) { if (value_t * record_p = queue_p->pop()) { return record_p; } }
        return nullptr;
    }
    Drain drain() noexcept { return utils::drain(*this); }
private:
    std::span<Queue *> m_queues;
    queue::intrusive::Queue<value_t> m_pending;
    InputSelectionPolicy m_input_policy;
    RequestSelectionPolicy m_request_policy;
    std::array<Source, 2> m_sources {Source::eIncoming, Source::ePending};
};

} // namespace lcf::async
