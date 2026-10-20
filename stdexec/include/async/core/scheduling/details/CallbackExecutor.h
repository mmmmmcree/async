#pragma once

#include <async/core/scheduling/details/queue.h>
#include <concepts>
#include <memory>
#include <utility>

namespace lcf::async::details {

template <typename Scheduler>
concept callback_target_c = std::copy_constructible<Scheduler> and std::equality_comparable<Scheduler>
    and requires(const Scheduler & scheduler, Callback * callback_p) {
        { scheduler == scheduler } noexcept -> std::convertible_to<bool>;
        { scheduler.enqueue(callback_p) } noexcept;
        { scheduler.workStarted() } noexcept;
        { scheduler.workFinished() } noexcept;
    };

class CallbackExecutor
{
    using Self = CallbackExecutor;
public:
    CallbackExecutor() noexcept = default;
    template <typename Scheduler>
        requires (not std::same_as<Scheduler, Self>) and callback_target_c<Scheduler>
    explicit CallbackExecutor(Scheduler scheduler) :
        m_owner_sp(std::make_shared<Scheduler>(std::move(scheduler))),
        m_enqueue([](const void * owner_p, Callback * callback_p) noexcept { static_cast<const Scheduler *>(owner_p)->enqueue(callback_p); }),
        m_work_started([](const void * owner_p) noexcept { static_cast<const Scheduler *>(owner_p)->workStarted(); }),
        m_work_finished([](const void * owner_p) noexcept { static_cast<const Scheduler *>(owner_p)->workFinished(); }),
        m_equal([](const void * first_p, const void * second_p) noexcept { return *static_cast<const Scheduler *>(first_p) == *static_cast<const Scheduler *>(second_p); }) {}
public:
    explicit operator bool() const noexcept { return static_cast<bool>(m_owner_sp); }
    bool operator==(const Self & other) const noexcept
    {
        if (m_owner_sp == other.m_owner_sp) { return true; }
        return m_equal == other.m_equal and m_equal and m_equal(m_owner_sp.get(), other.m_owner_sp.get());
    }
    void enqueue(Callback * callback_p) const noexcept { m_enqueue(m_owner_sp.get(), callback_p); }
    void workStarted() const noexcept { m_work_started(m_owner_sp.get()); }
    void workFinished() const noexcept { m_work_finished(m_owner_sp.get()); }
private:
    std::shared_ptr<const void> m_owner_sp;
    void (*m_enqueue)(const void *, Callback *) noexcept = nullptr;
    void (*m_work_started)(const void *) noexcept = nullptr;
    void (*m_work_finished)(const void *) noexcept = nullptr;
    bool (*m_equal)(const void *, const void *) noexcept = nullptr;
};

}
