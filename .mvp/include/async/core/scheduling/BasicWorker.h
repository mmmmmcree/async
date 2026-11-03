#pragma once

#include <async/core/thread_types.h>
#include <async/core/concepts.h>

#include <cassert>
#include <concepts>
#include <thread>

namespace lcf::async {

template <join_handle_c ThreadHandle>
struct ThreadLauncher
{
    ~ThreadLauncher() noexcept = default;
    ThreadLauncher() noexcept = default;
    ThreadLauncher(const ThreadLauncher &) noexcept = default;
    ThreadLauncher(ThreadLauncher &&) noexcept = default;
    ThreadLauncher & operator=(const ThreadLauncher &) noexcept = default;
    ThreadLauncher & operator=(ThreadLauncher &&) noexcept = default;
public:
    ThreadHandle operator()(ThreadEntry entry_f, void * state_p) const
    requires std::constructible_from<ThreadHandle, ThreadEntry, void *>
    {
        return ThreadHandle{entry_f, state_p};
    }
};

template <waiting_work_executor_c Executor, join_handle_c ThreadHandle = std::thread>
class BasicWorker
{
public:
    ~BasicWorker() noexcept
    {
        assert(not m_thread_handle.joinable() and "BasicWorker must be joined before destruction");
    }
    BasicWorker() = delete;
    template <thread_launcher_c<ThreadHandle> Launcher = ThreadLauncher<ThreadHandle>>
    explicit BasicWorker(Executor & executor, Launcher && launcher = {}) :
        m_executor_p(&executor), m_thread_handle(launcher(&run_executor, this)) {}
    BasicWorker(const BasicWorker &) = delete;
    BasicWorker & operator=(const BasicWorker &) = delete;
    BasicWorker(BasicWorker &&) = delete;
    BasicWorker & operator=(BasicWorker &&) = delete;
public:
    void join() noexcept
    {
        if (m_thread_handle.joinable()) { m_thread_handle.join(); }
    }
private:
    static void run_executor(void * state_p) noexcept
    {
        auto & worker = *static_cast<BasicWorker *>(state_p);
        while (worker.m_executor_p->waitAndExecuteOne()) {}
    }
private:
    Executor * m_executor_p;
    ThreadHandle m_thread_handle;
};

} // namespace lcf::async
