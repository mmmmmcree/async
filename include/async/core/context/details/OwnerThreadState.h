#pragma once

#include <atomic>
#include <exception>

namespace lcf::async::details {

class OwnerThreadState
{
    friend class OwnerThreadGuard;
public:
    ~OwnerThreadState() noexcept = default;
    OwnerThreadState() noexcept = default;
    OwnerThreadState(const OwnerThreadState &) = delete;
    OwnerThreadState(OwnerThreadState &&) = delete;
    OwnerThreadState & operator=(const OwnerThreadState &) = delete;
    OwnerThreadState & operator=(OwnerThreadState &&) = delete;
private:
    std::atomic_flag m_driving{};
};

class [[nodiscard]] OwnerThreadGuard
{
public:
    ~OwnerThreadGuard() noexcept { m_state_p->m_driving.clear(std::memory_order_release); }
    OwnerThreadGuard() = delete;
    explicit OwnerThreadGuard(OwnerThreadState & state) noexcept : m_state_p(&state)
    {
        if (state.m_driving.test_and_set(std::memory_order_acquire)) { std::terminate(); }
    }
    OwnerThreadGuard(const OwnerThreadGuard &) = delete;
    OwnerThreadGuard(OwnerThreadGuard &&) = delete;
    OwnerThreadGuard & operator=(const OwnerThreadGuard &) = delete;
    OwnerThreadGuard & operator=(OwnerThreadGuard &&) = delete;
private:
    OwnerThreadState * m_state_p;
};

} // namespace lcf::async::details
