#pragma once

#include <async/core/worker/thread/ThreadLauncher.h>
#include <async/core/worker/declarations/concepts.h>
#include <async/core/worker/declarations/enums.h>

#include <atomic>
#include <cassert>
#include <concepts>
#include <utility>

LCF_ASYNC_EXPORT
namespace lcf::async {

template <worker_progress_c Progress, worker_waiter_c Waiter, join_handle_c ThreadHandle = NullThreadHandle>
class BasicWorker
{
    static void run_worker(void * state_p) noexcept
    {
        auto & worker = *static_cast<BasicWorker *>(state_p);
        while (worker.waitAndExecuteOne()) {}
    }
public:
    ~BasicWorker() noexcept { this->join(); }
    BasicWorker() = delete;
    BasicWorker(Progress progress, Waiter & waiter) noexcept(
        std::is_nothrow_move_constructible_v<Progress> and std::is_nothrow_default_constructible_v<ThreadHandle>)
    requires std::default_initializable<ThreadHandle> :
        m_progress(std::move(progress)), m_waiter_p(&waiter) {}
    template <thread_launcher_c<ThreadHandle> Launcher>
    BasicWorker(Launcher & launcher, Progress progress, Waiter & waiter) :
        m_progress(std::move(progress)), m_waiter_p(&waiter),
        m_thread_handle(launcher(&run_worker, this)) {}
    BasicWorker(const BasicWorker &) = delete;
    BasicWorker(BasicWorker &&) = delete;
    BasicWorker & operator=(const BasicWorker &) = delete;
    BasicWorker & operator=(BasicWorker &&) = delete;
public:
    void run() noexcept
    {
        assert(not m_thread_handle.joinable());
        run_worker(this);
    }
    // Progress calls must not overlap.
    bool executeOne() noexcept
    {
        m_state.store(WorkerState::eExecuting, std::memory_order_relaxed);
        const bool progressed = m_progress();
        m_state.store(WorkerState::eIdle, std::memory_order_relaxed);
        return progressed;
    }
    bool waitAndExecuteOne() noexcept
    {
        for (;;) {
            const auto observed = m_waiter_p->observe();
            if (m_waiter_p->isStopRequested()) { break; }
            if (this->executeOne()) { return true; }
            if (m_waiter_p->isClosed()) { break; }
            m_state.store(WorkerState::eWaiting, std::memory_order_relaxed);
            m_waiter_p->wait(observed);
        }
        m_state.store(WorkerState::eStopped, std::memory_order_relaxed);
        return false;
    }
    void join() noexcept { if (m_thread_handle.joinable()) { m_thread_handle.join(); } }
    bool isJoinable() const noexcept { return m_thread_handle.joinable(); }
    WorkerState getState() const noexcept { return m_state.load(std::memory_order_relaxed); }
private:
    Progress m_progress;
    Waiter * m_waiter_p;
    std::atomic<WorkerState> m_state {WorkerState::eIdle};
    [[no_unique_address]] ThreadHandle m_thread_handle;
};

} // namespace lcf::async
