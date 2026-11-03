#pragma once

#include <async/core/concepts.h>
#include <async/core/scheduling/ScheduleNode.h>

#include <cassert>
#include <condition_variable>
#include <mutex>

namespace lcf::async {

class MutexInbox
{
public:
    using Node = ScheduleNode;
    static constexpr bool supports_multiple_producers = true;
    static constexpr bool supports_multiple_consumers = true;

    EnqueueResult tryEnqueue(ScheduleNode & node) noexcept
    {
        {
            std::lock_guard lock{m_mutex};
            if (m_closed) {
                return EnqueueResult::eClosed;
            }
            node.markQueued();
            if (m_tail_p) {
                m_tail_p->setNext(&node);
            } else {
                m_head_p = &node;
            }
            m_tail_p = &node;
        }
        m_condition.notify_one();
        return EnqueueResult::eAccepted;
    }

    ScheduleNode * tryPop() noexcept
    {
        std::lock_guard lock{m_mutex};
        return this->popFront();
    }

    ScheduleNode * waitPop() noexcept
    {
        std::unique_lock lock{m_mutex};
        m_condition.wait(lock, [this] {
            return m_stop_requested || m_closed || m_head_p != nullptr;
        });
        if (m_stop_requested) {
            return nullptr;
        }
        return this->popFront();
    }

    void requestStop() noexcept
    {
        {
            std::lock_guard lock{m_mutex};
            m_stop_requested = true;
        }
        m_condition.notify_all();
    }

    void close() noexcept
    {
        {
            std::lock_guard lock{m_mutex};
            m_closed = true;
        }
        m_condition.notify_all();
    }

    bool empty() const noexcept
    {
        std::lock_guard lock{m_mutex};
        return m_head_p == nullptr;
    }

private:
    // The caller holds m_mutex. Never dispatch user code while holding it.
    ScheduleNode * popFront() noexcept
    {
        auto * node_p = m_head_p;
        if (not node_p) {
            return nullptr;
        }
        m_head_p = node_p->getNext();
        if (not m_head_p) {
            m_tail_p = nullptr;
        }
        node_p->clearQueueState();
        return node_p;
    }

    mutable std::mutex m_mutex;
    std::condition_variable m_condition;
    ScheduleNode * m_head_p = nullptr;
    ScheduleNode * m_tail_p = nullptr;
    bool m_closed = false;
    bool m_stop_requested = false;
};

} // namespace lcf::async
