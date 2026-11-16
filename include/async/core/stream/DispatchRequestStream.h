#pragma once

#include <async/config/macros.h>
#include <async/core/queue/intrusive/Queue.h>
#include <async/core/queue/utils/pop_one.h>
#include <async/core/policy/utils/selected.h>
#include <async/core/stream/declarations/concepts.h>
#include <async/core/stream/utils/pull_all.h>

#include <algorithm>
#include <array>
#include <ranges>
#include <span>
#include <utility>

LCF_ASYNC_EXPORT
namespace lcf::async {

template <consumer_queue_c Queue, typename InputSelectionPolicy, typename RequestSelectionPolicy>
class DispatchRequestStream
{
    enum class Source { eIncoming, ePending };
    using PullAll = utils::details::PullAllView<DispatchRequestStream *>;
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
    value_t * pullOne() noexcept
    {
        for (const Source source : m_sources | utils::selected(m_request_policy)) {
            value_t * record_p = source == Source::ePending
                ? queue::utils::pop_one(m_pending)
                : queue::utils::pop_one(m_queues | utils::selected(m_input_policy));
            if (record_p) { return record_p; }
        }
        return nullptr;
    }
    value_t * pullAny() noexcept
    {
        if (value_t * record_p = queue::utils::pop_one(m_pending)) { return record_p; }
        return queue::utils::pop_one(m_queues);
    }
    PullAll pullAll() noexcept { return utils::pull_all(*this); }
    void requeue(value_t & record) noexcept { m_pending.pushBack(record); }
    bool isEmpty() const noexcept
    {
        return m_pending.isEmpty() and std::ranges::all_of(m_queues, [](Queue * queue_p) { return queue_p->isEmpty(); });
    }
private:
    std::span<Queue *> m_queues;
    queue::intrusive::Queue<value_t> m_pending;
    InputSelectionPolicy m_input_policy;
    RequestSelectionPolicy m_request_policy;
    std::array<Source, 2> m_sources {Source::eIncoming, Source::ePending};
};

} // namespace lcf::async
