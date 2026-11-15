#pragma once

#include <async/core/queue/declarations/concepts.h>
#include <async/core/queue/utils/pop_one.h>

#include <memory>
#include <span>

LCF_ASYNC_EXPORT
namespace lcf::async::queue {

template <typename Value>
class QueueView
{
public:
    using value_t = Value;
public:
    ~QueueView() noexcept = default;
    QueueView() = delete;
    template <consumer_queue_c Queue>
    requires (not std::same_as<Queue, QueueView>) and
        std::same_as<typename Queue::value_t, Value>
    explicit QueueView(Queue & queue) noexcept :
        m_queue_p(std::addressof(queue)),
        m_try_pop_f([](void * queue_p) noexcept { return queue::utils::pop_one(*static_cast<Queue *>(queue_p)); }),
        m_is_empty_f([](const void * queue_p) noexcept { return static_cast<const Queue *>(queue_p)->isEmpty(); }) {}
    QueueView(const QueueView &) noexcept = default;
    QueueView(QueueView &&) noexcept = default;
    QueueView & operator=(const QueueView &) noexcept = default;
    QueueView & operator=(QueueView &&) noexcept = default;
public:
    Value * tryPop() noexcept { return m_try_pop_f(m_queue_p); }
    bool isEmpty() const noexcept { return m_is_empty_f(m_queue_p); }
private:
    void * m_queue_p;
    Value * (*m_try_pop_f)(void *) noexcept;
    bool (*m_is_empty_f)(const void *) noexcept;
};

template <typename Value>
using queue_view_span_t = std::span<QueueView<Value>>;

} // namespace lcf::async::queue
