#pragma once

#include <atomic>
#include <cstdint>

namespace lcf::async::details {

class WorkSignal
{
public:
    ~WorkSignal() noexcept = default;
    WorkSignal() noexcept = default;
    WorkSignal(const WorkSignal &) = delete;
    WorkSignal(WorkSignal &&) = delete;
    WorkSignal & operator=(const WorkSignal &) = delete;
    WorkSignal & operator=(WorkSignal &&) = delete;
public:
    std::uint64_t observe() const noexcept { return m_sequence.load(std::memory_order_acquire); }
    void wait(std::uint64_t observed) const noexcept { m_sequence.wait(observed, std::memory_order_acquire); }
    void notifyOne() noexcept
    {
        m_sequence.fetch_add(1, std::memory_order_release);
        m_sequence.notify_one();
    }
    void notifyAll() noexcept
    {
        m_sequence.fetch_add(1, std::memory_order_release);
        m_sequence.notify_all();
    }
private:
    std::atomic<std::uint64_t> m_sequence{0};
};

} // namespace lcf::async::details
