#pragma once

#include <async/core/scheduling/details/EventSource.h>
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

template <typename Scheduler>
concept event_target_c = requires(const Scheduler & scheduler) {
    { scheduler.getEventSource() } noexcept -> std::same_as<const EventSource &>;
};

class CallbackExecutor
{
    using Self = CallbackExecutor;
    struct Operations
    {
        void (*m_enqueue)(const void *, Callback *) noexcept;
        void (*m_work_started)(const void *) noexcept;
        void (*m_work_finished)(const void *) noexcept;
        bool (*m_equal)(const void *, const void *) noexcept;
        const EventSource * (*m_get_event_source)(const void *) noexcept;
    };
public:
    CallbackExecutor() noexcept = default;
    template <typename Scheduler>
        requires (not std::same_as<Scheduler, Self>) and callback_target_c<Scheduler>
    explicit CallbackExecutor(Scheduler scheduler) :
        m_owner_sp(std::make_shared<Scheduler>(std::move(scheduler))), m_operations_p(&k_operations<Scheduler>) {}
public:
    explicit operator bool() const noexcept { return static_cast<bool>(m_owner_sp); }
    bool operator==(const Self & other) const noexcept
    {
        if (m_owner_sp == other.m_owner_sp) { return true; }
        return m_operations_p == other.m_operations_p and m_operations_p
            and m_operations_p->m_equal(m_owner_sp.get(), other.m_owner_sp.get());
    }
    void enqueue(Callback * callback_p) const noexcept { m_operations_p->m_enqueue(m_owner_sp.get(), callback_p); }
    void workStarted() const noexcept { m_operations_p->m_work_started(m_owner_sp.get()); }
    void workFinished() const noexcept { m_operations_p->m_work_finished(m_owner_sp.get()); }
    const EventSource * getEventSource() const noexcept
    {
        return m_operations_p ? m_operations_p->m_get_event_source(m_owner_sp.get()) : nullptr;
    }
private:
    template <typename Scheduler>
    static const EventSource * get_event_source(const void * owner_p) noexcept
    {
        if constexpr (event_target_c<Scheduler>) { return &static_cast<const Scheduler *>(owner_p)->getEventSource(); }
        else { return nullptr; }
    }
private:
    template <typename Scheduler>
    static inline constexpr Operations k_operations {
        [](const void * owner_p, Callback * callback_p) noexcept { static_cast<const Scheduler *>(owner_p)->enqueue(callback_p); },
        [](const void * owner_p) noexcept { static_cast<const Scheduler *>(owner_p)->workStarted(); },
        [](const void * owner_p) noexcept { static_cast<const Scheduler *>(owner_p)->workFinished(); },
        [](const void * first_p, const void * second_p) noexcept { return *static_cast<const Scheduler *>(first_p) == *static_cast<const Scheduler *>(second_p); },
        &Self::get_event_source<Scheduler>};
    std::shared_ptr<const void> m_owner_sp;
    const Operations * m_operations_p = nullptr;
};

}
