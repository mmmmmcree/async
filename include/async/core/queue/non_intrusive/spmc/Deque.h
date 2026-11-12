#pragma once

#include <async/config/macros.h>

#include <array>
#include <cstdint>
#include <mutex>
#include <system_error>

LCF_ASYNC_EXPORT
namespace lcf::async::queue::non_intrusive::spmc {

// One owner pushes/pops the back; thieves remove from the front.
template <typename Item, std::uint64_t queue_capacity = 256>
requires (queue_capacity > 0)
class Deque
{
public:
    using Value = Item;
    static constexpr bool k_supports_multiple_producers = false;
    static constexpr bool k_supports_multiple_consumers = true;
public:
    Deque() = default;
    Deque(const Deque &) = delete;
    Deque(Deque &&) = delete;
    Deque & operator=(const Deque &) = delete;
    Deque & operator=(Deque &&) = delete;
public:
    std::error_code tryEnqueue(Value & value) noexcept
    {
        std::lock_guard lock{m_mutex};
        if (m_closed) { return std::make_error_code(std::errc::operation_canceled); }
        if (m_size == queue_capacity) { return std::make_error_code(std::errc::no_buffer_space); }
        m_slots[(m_front + m_size) % queue_capacity] = &value;
        ++m_size;
        return {};
    }
    Value * tryPopBack() noexcept
    {
        std::lock_guard lock{m_mutex};
        if (m_size == 0) { return nullptr; }
        --m_size;
        return m_slots[(m_front + m_size) % queue_capacity];
    }
    Value * tryPopFront() noexcept
    {
        std::lock_guard lock{m_mutex};
        if (m_size == 0) { return nullptr; }
        auto * value_p = m_slots[m_front];
        m_front = (m_front + 1) % queue_capacity;
        --m_size;
        return value_p;
    }
    void close() noexcept
    {
        std::lock_guard lock{m_mutex};
        m_closed = true;
    }
    bool isEmpty() const noexcept
    {
        std::lock_guard lock{m_mutex};
        return m_size == 0;
    }
private:
    mutable std::mutex m_mutex;
    std::array<Value *, queue_capacity> m_slots{};
    std::uint64_t m_front = 0;
    std::uint64_t m_size = 0;
    bool m_closed = false;
};

} // namespace lcf::async::queue::non_intrusive::spmc
