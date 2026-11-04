#pragma once

#include <atomic>
#include <cassert>
#include <exception>
#include <thread>

namespace lcf::async::details {

class OwnerThreadBinding
{
public:
    ~OwnerThreadBinding() noexcept = default;
    OwnerThreadBinding() noexcept = default;
    OwnerThreadBinding(const OwnerThreadBinding &) = delete;
    OwnerThreadBinding(OwnerThreadBinding &&) = delete;
    OwnerThreadBinding & operator=(const OwnerThreadBinding &) = delete;
    OwnerThreadBinding & operator=(OwnerThreadBinding &&) = delete;
public:
    void enter() noexcept
    {
        if (m_driving.test_and_set(std::memory_order_acquire)) { std::terminate(); }
        const auto thread_id = std::this_thread::get_id();
        if (m_owner == std::thread::id{}) { m_owner = thread_id; }
        assert(m_owner == thread_id and "Context must be driven by the same owner thread");
    }
    void leave() noexcept { m_driving.clear(std::memory_order_release); }
private:
    std::atomic_flag m_driving{};
    std::thread::id m_owner;
};

} // namespace lcf::async::details
