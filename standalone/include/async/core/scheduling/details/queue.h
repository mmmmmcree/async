#pragma once

#include <async/core/scheduling/details/EventSource.h>
#include <condition_variable>
#include <cstddef>
#include <mutex>

namespace lcf::async::details {

class Queue
{
    using Self = Queue;
public:
    class Transaction
    {
        friend class Queue;
        Transaction(Queue & queue, std::unique_lock<std::mutex> & lock) noexcept : m_queue(queue), m_lock(lock) {}
    public:
        ~Transaction() noexcept
        {
            m_lock.unlock();
            if (m_notify) { m_queue.m_changed.notify_one(); }
        }
        Transaction(const Transaction &) = delete;
        Transaction & operator=(const Transaction &) = delete;
    public:
        bool isClosed() const noexcept { return m_queue.m_closed; }
        void push(Callback * callback_p) noexcept { m_queue.pushReady(callback_p); m_notify = true; }
        void wakeEvents() noexcept { m_queue.queueEventCheck(); m_notify = true; }
    private:
        Queue & m_queue;
        std::unique_lock<std::mutex> & m_lock;
        bool m_notify = false;
    };
public:
    ~Queue() noexcept { this->shutdown(); }
    Queue() = default;
    template <typename Factory>
        requires requires(Factory factory, Self & queue) { { factory(queue) } -> std::same_as<EventSource>; }
    explicit Queue(Factory factory) : m_source(factory(*this)) {}
    Queue(const Self &) = delete;
    Self & operator=(const Self &) = delete;
public:
    template <typename Function>
    auto synchronize(Function function) noexcept(noexcept(function(std::declval<Transaction &>())))
    {
        std::unique_lock lock {m_mutex};
        Transaction transaction {*this, lock};
        return function(transaction);
    }
    void push(Callback * callback_p) noexcept
    {
        {
            std::unique_lock lock {m_mutex};
            if (m_closed) {
                lock.unlock();
                if (callback_p->isOwned()) { callback_p->abandon(); }
                return;
            }
            this->pushReady(callback_p);
        }
        m_changed.notify_one();
    }
    const EventSource & getEventSource() const noexcept { return m_source; }
    void wakeEvents() noexcept
    {
        {
            std::lock_guard lock {m_mutex};
            if (m_closed) { return; }
            this->queueEventCheck();
        }
        m_changed.notify_one();
    }
    Callback * pop(bool wait, bool until_idle = false)
    {
        std::unique_lock lock {m_mutex};
        if (m_stopped) { return nullptr; }
        if (m_head_p and m_head_p != &m_event_check) { return this->takeReady(); }
        if (not m_head_p and not wait) { return nullptr; }
        return this->popSlow(lock, wait, until_idle);
    }
    void stop() noexcept
    {
        { std::lock_guard lock {m_mutex}; m_stopped = true; }
        m_changed.notify_all();
    }
    void restart() noexcept { std::lock_guard lock {m_mutex}; if (not m_closed) { m_stopped = false; } }
    void shutdown() noexcept
    {
        std::unique_lock lock {m_mutex};
        if (m_closed) { return; }
        m_closed = true;
        m_stopped = true;
        Callback * callbacks_p = nullptr;
        auto ** tail_pp = &callbacks_p;
        while (m_head_p) {
            auto * callback_p = m_head_p;
            m_head_p = callback_p->m_next_p;
            callback_p->m_next_p = nullptr;
            if (callback_p->isOwned()) { *tail_pp = callback_p; tail_pp = &callback_p->m_next_p; }
        }
        m_tail_p = nullptr;
        m_event_check_queued = false;
        m_ready_count = 0;
        lock.unlock();
        m_changed.notify_all();
        // Sources can finish work accounting and unregister callbacks here.
        if (m_source) { m_source.shutdown(); }
        while (callbacks_p) {
            auto * callback_p = callbacks_p;
            callbacks_p = callbacks_p->m_next_p;
            callback_p->abandon();
        }
    }
    std::size_t readyCount() noexcept
    {
        std::lock_guard lock {m_mutex};
        if (not m_source) { return m_ready_count; }
        return m_ready_count + m_source.readyCount(std::chrono::steady_clock::now());
    }
    void workStarted() noexcept { std::lock_guard lock {m_mutex}; ++m_work_count; }
    void workFinished() noexcept
    {
        { std::lock_guard lock {m_mutex}; --m_work_count; }
        m_changed.notify_all();
    }
private:
    void append(Callback * callback_p) noexcept
    {
        if (m_tail_p) { m_tail_p->m_next_p = callback_p; }
        else { m_head_p = callback_p; }
        m_tail_p = callback_p;
    }
    void pushReady(Callback * callback_p) noexcept
    {
        this->append(callback_p);
        ++m_ready_count;
    }
    Callback * removeFront() noexcept
    {
        auto * callback_p = m_head_p;
        m_head_p = callback_p->m_next_p;
        if (not m_head_p) { m_tail_p = nullptr; }
        callback_p->m_next_p = nullptr;
        return callback_p;
    }
    Callback * takeReady() noexcept { --m_ready_count; return this->removeFront(); }
    void queueEventCheck() noexcept
    {
        if (not m_event_check_queued) {
            this->append(&m_event_check);
            m_event_check_queued = true;
        }
    }
    Callback * popSlow(std::unique_lock<std::mutex> & lock, bool wait, bool until_idle)
    {
        for (;;) {
            if (m_stopped) { return nullptr; }
            if (m_head_p == &m_event_check) {
                this->removeFront();
                m_event_check_queued = false;
            }
            const auto event = m_source ? m_source.poll() : EventPoll {};
            const bool pending = event.m_deadline != std::chrono::steady_clock::time_point::max();
            if (pending) { this->queueEventCheck(); }
            if (event.m_callback_p) { return event.m_callback_p; }
            if (m_head_p and m_head_p != &m_event_check) { return this->takeReady(); }
            if (not wait or (until_idle and m_work_count == 0)) { return nullptr; }
            if (pending) { m_changed.wait_until(lock, event.m_deadline); }
            else { m_changed.wait(lock); }
        }
    }
private:
    std::mutex m_mutex;
    std::condition_variable m_changed;
    Callback * m_head_p = nullptr;
    Callback * m_tail_p = nullptr;
    bool m_stopped = false;
    bool m_closed = false;
    bool m_event_check_queued = false;
    std::size_t m_work_count = 0;
    std::size_t m_ready_count = 0;
    Callback m_event_check;
    EventSource m_source;
};

}
