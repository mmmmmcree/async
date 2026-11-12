#pragma once

#include <async/core/queue/declarations/concepts.h>

#include <cstddef>
#include <functional>
#include <iterator>
#include <ranges>
#include <utility>

LCF_ASYNC_EXPORT
namespace lcf::async::queue::utils {

enum class DrainDirection
{
    from_front,
    from_back,
};

using enum DrainDirection;

namespace details {

// begin() pops the first value; each increment pops the next. The queue must outlive iteration.
template <queue_c Queue, DrainDirection direction = from_front>
class DrainView : public std::ranges::view_interface<DrainView<Queue, direction>>
{
    using Value = typename Queue::Value;

    class Iterator
    {
    public:
        using iterator_concept = std::input_iterator_tag;
        using value_type = Value;
        using difference_type = std::ptrdiff_t;
    public:
        ~Iterator() noexcept = default;
        Iterator() = delete;
        explicit Iterator(Queue & queue) noexcept : m_queue(queue) {}
        Iterator(const Iterator &) = delete;
        Iterator(Iterator &&) noexcept = default;
        Iterator & operator=(const Iterator &) = delete;
        Iterator & operator=(Iterator &&) noexcept = default;
    public:
        Value & operator*() const noexcept { return *m_value_p; }
        Iterator & operator++() noexcept
        {
            if constexpr (deque_c<Queue>) {
                if constexpr (direction == from_front) { m_value_p = m_queue.get().tryPopFront(); }
                else { m_value_p = m_queue.get().tryPopBack(); }
            } else { m_value_p = m_queue.get().tryPop(); }
            return *this;
        }
        void operator++(int) noexcept { ++*this; }
        bool operator==(std::default_sentinel_t) const noexcept { return not m_value_p; }
    private:
        std::reference_wrapper<Queue> m_queue;
        Value * m_value_p = nullptr;
    };
public:
    ~DrainView() noexcept = default;
    DrainView() = delete;
    explicit DrainView(Queue & queue) noexcept : m_queue(queue) {}
    DrainView(const DrainView &) noexcept = default;
    DrainView(DrainView &&) noexcept = default;
    DrainView & operator=(const DrainView &) noexcept = default;
    DrainView & operator=(DrainView &&) noexcept = default;
public:
    Iterator begin() const noexcept { return std::move(++Iterator {m_queue.get()}); }
    std::default_sentinel_t end() const noexcept { return {}; }
private:
    std::reference_wrapper<Queue> m_queue;
};

} // namespace details

template <DrainDirection direction = from_front, queue_c Queue>
details::DrainView<Queue, direction> drain(Queue & queue) noexcept { return details::DrainView<Queue, direction> {queue}; }

} // namespace lcf::async::queue::utils
