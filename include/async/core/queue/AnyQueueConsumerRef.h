#pragma once

#include <async/core/queue/declarations/concepts.h>
#include <async/core/queue/utils/pop_one.h>

#include <memory>
#include <span>

LCF_ASYNC_EXPORT
namespace lcf::async::queue {

template <typename Value>
class AnyQueueConsumerRef
{
public:
    using value_t = Value;
public:
    ~AnyQueueConsumerRef() noexcept = default;
    AnyQueueConsumerRef() = delete;
    template <consumer_queue_c Queue>
    requires (not std::same_as<Queue, AnyQueueConsumerRef>) and
        std::same_as<typename Queue::value_t, Value>
    explicit AnyQueueConsumerRef(Queue & queue) noexcept :
        m_queue_p(std::addressof(queue)),
        m_pop_f([](void * queue_p) noexcept { return queue::utils::pop_one(*static_cast<Queue *>(queue_p)); }),
        m_is_empty_f([](const void * queue_p) noexcept { return static_cast<const Queue *>(queue_p)->isEmpty(); }) {}
    AnyQueueConsumerRef(const AnyQueueConsumerRef &) noexcept = default;
    AnyQueueConsumerRef(AnyQueueConsumerRef &&) noexcept = default;
    AnyQueueConsumerRef & operator=(const AnyQueueConsumerRef &) noexcept = default;
    AnyQueueConsumerRef & operator=(AnyQueueConsumerRef &&) noexcept = default;
public:
    Value * pop() noexcept { return m_pop_f(m_queue_p); }
    bool isEmpty() const noexcept { return m_is_empty_f(m_queue_p); }
private:
    void * m_queue_p;
    Value * (*m_pop_f)(void *) noexcept;
    bool (*m_is_empty_f)(const void *) noexcept;
};

template <typename Value>
using any_queue_consumer_ref_span_t = std::span<AnyQueueConsumerRef<Value>>;

} // namespace lcf::async::queue
