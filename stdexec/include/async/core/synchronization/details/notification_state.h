#pragma once

#include <atomic>
#include <cstddef>
#include <exception>
#include <memory>
#include <memory_resource>
#include <mutex>
#include <utility>

namespace lcf::async::details {

struct NotificationWaiter
{
    void complete(std::exception_ptr exception = {}) noexcept { m_complete(this, std::move(exception)); }
    bool dispatch(std::shared_ptr<NotificationWaiter> self_sp) noexcept { return m_dispatch(std::move(self_sp)); }
    bool isCompleted() const noexcept { return m_done.load(std::memory_order_acquire); }
    void (*m_complete)(NotificationWaiter *, std::exception_ptr) noexcept;
    bool (*m_dispatch)(std::shared_ptr<NotificationWaiter>) noexcept;
    std::atomic_bool m_done = false;
    std::shared_ptr<NotificationWaiter> m_next_sp;
    NotificationWaiter * m_previous_p = nullptr;
    bool m_linked = false;
};

class Notification
{
    using WaiterHandle = std::shared_ptr<NotificationWaiter>;
public:
    Notification(std::size_t remaining, std::pmr::memory_resource * = std::pmr::get_default_resource()) noexcept : m_remaining(remaining) {}
    bool isReady() const noexcept { return m_ready.load(std::memory_order_acquire); }
    void subscribe(WaiterHandle waiter_sp)
    {
        bool dispatch = false;
        {
            std::lock_guard lock {m_mutex};
            if (waiter_sp->isCompleted()) { return; }
            waiter_sp->m_previous_p = m_tail_p;
            waiter_sp->m_linked = true;
            auto * waiter_p = waiter_sp.get();
            if (m_tail_p) { m_tail_p->m_next_sp = std::move(waiter_sp); }
            else { m_head_sp = std::move(waiter_sp); }
            m_tail_p = waiter_p;
            if (this->isReady() and not m_draining) { m_draining = true; dispatch = true; }
        }
        if (dispatch) { this->dispatchNext(); }
    }
    void remove(NotificationWaiter * waiter_p) noexcept
    {
        std::lock_guard lock {m_mutex};
        this->unlink(waiter_p);
    }
    void complete() noexcept
    {
        {
            std::lock_guard lock {m_mutex};
            if (--m_remaining != 0) { return; }
            m_ready.store(true, std::memory_order_release);
            m_draining = true;
        }
        this->dispatchNext();
    }
    void dispatchNext() noexcept
    {
        for (;;) {
            WaiterHandle waiter_sp;
            {
                std::lock_guard lock {m_mutex};
                if (not m_head_sp) { m_draining = false; return; }
                waiter_sp = m_head_sp;
                this->unlink(waiter_sp.get());
            }
            if (waiter_sp->dispatch(std::move(waiter_sp))) { return; }
        }
    }
private:
    void unlink(NotificationWaiter * waiter_p) noexcept
    {
        if (not waiter_p->m_linked) { return; }
        auto next_sp = std::move(waiter_p->m_next_sp);
        auto * previous_p = waiter_p->m_previous_p;
        if (next_sp) { next_sp->m_previous_p = previous_p; }
        else { m_tail_p = previous_p; }
        if (previous_p) { previous_p->m_next_sp = std::move(next_sp); }
        else { m_head_sp = std::move(next_sp); }
        waiter_p->m_previous_p = nullptr;
        waiter_p->m_linked = false;
    }
private:
    WaiterHandle m_head_sp;
    NotificationWaiter * m_tail_p = nullptr;
    std::mutex m_mutex;
    std::size_t m_remaining;
    std::atomic_bool m_ready = false;
    bool m_draining = false;
};


}
