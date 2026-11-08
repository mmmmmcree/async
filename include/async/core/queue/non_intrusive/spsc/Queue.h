#pragma once

#include <async/config/macros.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <system_error>

LCF_ASYNC_EXPORT
namespace lcf::async::queue::non_intrusive::spsc {

// close() requires the producer to be quiescent.
template <typename Item, std::size_t queue_capacity = 256>
requires (queue_capacity > 0)
class Queue
{
public:
    using Value = Item;
    static constexpr bool k_supports_multiple_producers = false;
    static constexpr bool k_supports_multiple_consumers = false;
public:
    Queue() noexcept = default;
    Queue(const Queue &) = delete;
    Queue(Queue &&) = delete;
    Queue & operator=(const Queue &) = delete;
    Queue & operator=(Queue &&) = delete;
public:
    std::error_code tryEnqueue(Value & value) noexcept
    {
        if (m_closed.load(std::memory_order_acquire)) { return std::make_error_code(std::errc::operation_canceled); }
        const auto write = m_write.load(std::memory_order_relaxed);
        const auto next = advance(write);
        if (next == m_read.load(std::memory_order_acquire)) { return std::make_error_code(std::errc::no_buffer_space); }
        m_slots[write] = &value;
        m_write.store(next, std::memory_order_release);
        return {};
    }
    Value * tryPop() noexcept
    {
        const auto read = m_read.load(std::memory_order_relaxed);
        if (read == m_write.load(std::memory_order_acquire)) { return nullptr; }
        auto * value_p = m_slots[read];
        m_read.store(advance(read), std::memory_order_release);
        return value_p;
    }
    void close() noexcept { m_closed.store(true, std::memory_order_release); }
    bool isEmpty() const noexcept
    {
        return m_read.load(std::memory_order_acquire) == m_write.load(std::memory_order_acquire);
    }
    static constexpr std::size_t capacity() noexcept { return queue_capacity; }
private:
    static constexpr std::size_t advance(std::size_t index) noexcept { return index == queue_capacity ? 0 : index + 1; }
private:
    std::array<Value *, queue_capacity + 1> m_slots{};
    alignas(64) std::atomic<std::size_t> m_write{0};
    alignas(64) std::atomic<std::size_t> m_read{0};
    std::atomic<bool> m_closed{false};
};

} // namespace lcf::async::queue::non_intrusive::spsc
