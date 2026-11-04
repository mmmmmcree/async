#pragma once

#include <async/core/worker/selectors/RoundRobinSelector.h>
#include <async/core/worker/WorkerPool.h>

#include <span>
#include <type_traits>
#include <utility>

namespace lcf::async::details {

template <multi_worker_executor_c Executor, join_handle_c ThreadHandle, work_executor_c LoopExecutor>
class ContextSelector
{
    using Pool = WorkerPool<Executor, ThreadHandle>;
    using Selector = RoundRobinSelector<Executor>;
public:
    ~ContextSelector() noexcept = default;
    ContextSelector() noexcept = default;
    ContextSelector(LoopExecutor & owner_executor, Pool & worker_pool, std::span<Executor> executors) noexcept :
        m_owner_executor_p(&owner_executor), m_worker_pool_p(&worker_pool), m_shared_selector(executors) {}
    ContextSelector(const ContextSelector &) noexcept = default;
    ContextSelector(ContextSelector &&) noexcept = default;
    ContextSelector & operator=(const ContextSelector &) noexcept = default;
    ContextSelector & operator=(ContextSelector &&) noexcept = default;
public:
    void beginSelection() noexcept
    {
        m_include_shared = not m_worker_pool_p->isRunning();
        m_shared_first = m_include_shared and m_next_shared_first;
        m_next_shared_first = not m_next_shared_first;
        m_owner_pending = true;
        m_shared_selector.beginSelection();
    }
    template <typename Visitor>
    requires std::is_nothrow_invocable_r_v<void, Visitor &, Executor &>
        and std::is_nothrow_invocable_r_v<void, Visitor &, LoopExecutor &>
    bool visitNext(Visitor && visitor) noexcept
    {
        if (m_shared_first and m_shared_selector.visitNext(visitor)) { return true; }
        if (std::exchange(m_owner_pending, false)) {
            visitor(*m_owner_executor_p);
            return true;
        }
        return m_include_shared and not m_shared_first and m_shared_selector.visitNext(visitor);
    }
    bool isEmpty() const noexcept
    {
        return m_owner_executor_p->isEmpty()
            and (m_worker_pool_p->isRunning() or m_shared_selector.isEmpty());
    }
private:
    LoopExecutor * m_owner_executor_p = nullptr;
    Pool * m_worker_pool_p = nullptr;
    Selector m_shared_selector;
    bool m_include_shared = false;
    bool m_shared_first = false;
    bool m_next_shared_first = false;
    bool m_owner_pending = false;
};

} // namespace lcf::async::details
