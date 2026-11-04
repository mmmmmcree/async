#pragma once

#include <async/core/worker/declarations/concepts.h>
#include <async/core/worker/selectors/declarations/concepts.h>

#include <atomic>
#include <concepts>
#include <type_traits>
#include <utility>

LCF_ASYNC_EXPORT
namespace lcf::async {

template <executor_selector_c Selector, worker_waiter_c Waiter>
class WorkerDriver
{
public:
    ~WorkerDriver() noexcept = default;
    WorkerDriver() noexcept(std::is_nothrow_default_constructible_v<Selector>) = default;
    WorkerDriver(Selector selector, Waiter & waiter) noexcept(std::is_nothrow_move_constructible_v<Selector>) :
        m_selector(std::move(selector)), m_waiter_p(&waiter) {}
    WorkerDriver(const WorkerDriver &) = delete;
    WorkerDriver(WorkerDriver &&) = delete;
    WorkerDriver & operator=(const WorkerDriver &) = delete;
    WorkerDriver & operator=(WorkerDriver &&) = delete;
public:
    void create(Selector selector, Waiter & waiter) noexcept(std::is_nothrow_move_assignable_v<Selector>)
    requires std::assignable_from<Selector &, Selector>
    {
        m_selector = std::move(selector);
        m_waiter_p = &waiter;
        m_state.store(WorkerState::eIdle, std::memory_order_relaxed);
    }
    bool executeOne() noexcept
    {
        m_state.store(WorkerState::eSearching, std::memory_order_relaxed);
        m_selector.beginSelection();
        bool executed = false;
        while (m_selector.visitNext([&](work_executor_c auto & executor) noexcept {
            m_state.store(WorkerState::eExecuting, std::memory_order_relaxed);
            executed = executor.executeOne();
        })) {
            if (executed) {
                m_state.store(WorkerState::eIdle, std::memory_order_relaxed);
                return true;
            }
            m_state.store(WorkerState::eSearching, std::memory_order_relaxed);
        }
        m_state.store(WorkerState::eIdle, std::memory_order_relaxed);
        return false;
    }
    bool waitAndExecuteOne() noexcept
    {
        for (;;) {
            const auto observed = m_waiter_p->observe();
            if (m_waiter_p->isStopRequested()) { break; }
            if (this->executeOne()) { return true; }
            if (m_waiter_p->isClosed() and m_selector.isEmpty()) { break; }
            m_state.store(WorkerState::eWaiting, std::memory_order_relaxed);
            m_waiter_p->wait(observed);
        }
        m_state.store(WorkerState::eStopped, std::memory_order_relaxed);
        return false;
    }
    void run() noexcept { while (this->waitAndExecuteOne()) {} }
    WorkerState getState() const noexcept { return m_state.load(std::memory_order_relaxed); }
private:
    Selector m_selector;
    Waiter * m_waiter_p = nullptr;
    std::atomic<WorkerState> m_state{WorkerState::eIdle};
};

} // namespace lcf::async
