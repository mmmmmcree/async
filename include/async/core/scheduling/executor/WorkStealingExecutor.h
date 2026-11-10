#pragma once

#include <async/core/queue/declarations/concepts.h>
#include <async/core/notify/declarations/concepts.h>
#include <async/core/scheduling/executor/BasicExecutor.h>

#include <cstdint>
#include <type_traits>
#include <utility>

LCF_ASYNC_EXPORT
namespace lcf::async {

template <work_queue_c Inbox, stealable_work_queue_c ReadyQueue,
    stealable_work_source_c VictimSelector, notify::notifier_c Notifier>
requires std::same_as<typename Inbox::Value, typename ReadyQueue::Value>
    and std::same_as<typename Inbox::Value, typename VictimSelector::Value>
class WorkStealingExecutor
{
public:
    ~WorkStealingExecutor() noexcept = default;
    WorkStealingExecutor() = delete;
    WorkStealingExecutor(Inbox & inbox, ReadyQueue & ready, VictimSelector victim_selector, Notifier notifier = {}) noexcept(
        std::is_nothrow_move_constructible_v<VictimSelector> and std::is_nothrow_move_constructible_v<Notifier>) :
        m_inbox_p(&inbox), m_ready_p(&ready), m_victim_selector(std::move(victim_selector)), m_notifier(std::move(notifier)) {}
    WorkStealingExecutor(const WorkStealingExecutor &) = default;
    WorkStealingExecutor(WorkStealingExecutor &&) = default;
    WorkStealingExecutor & operator=(const WorkStealingExecutor &) = default;
    WorkStealingExecutor & operator=(WorkStealingExecutor &&) = default;
public:
    bool executeOne() noexcept
    {
        if (BasicExecutor{*m_ready_p}.executeOne()) { return true; }
        bool published = false;
        for (std::uint64_t count = 0; count < 16; ++count) {
            auto * incoming_p = m_inbox_p->tryPop();
            if (not incoming_p) { break; }
            if (m_ready_p->tryEnqueue(*incoming_p)) {
                if (published) { m_notifier.notifyAll(); }
                incoming_p->execute();
                return true;
            }
            published = true;
        }
        // Wake thieves before entering user code, which may block indefinitely.
        if (published) { m_notifier.notifyAll(); }
        if (BasicExecutor{*m_ready_p}.executeOne()) { return true; }
        if (auto * executable_p = m_victim_selector.trySteal()) {
            executable_p->execute();
            return true;
        }
        return false;
    }
    bool isEmpty() const noexcept { return m_inbox_p->isEmpty() and m_ready_p->isEmpty(); }
private:
    Inbox * m_inbox_p;
    ReadyQueue * m_ready_p;
    VictimSelector m_victim_selector;
    Notifier m_notifier;
};

} // namespace lcf::async
