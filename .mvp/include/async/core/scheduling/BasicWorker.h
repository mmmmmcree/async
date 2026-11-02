#pragma once

#include <async/core/thread_types.h>
#include <async/core/concepts.h>

#include <cassert>
#include <thread>

namespace lcf::async {

struct StdThreadFactory
{
    std::thread operator()(ThreadEntry entry_f, void * state_p) const
    {
        return std::thread{entry_f, state_p};
    }
};

template <blocking_work_executor_c Executor, thread_c Thread = std::thread>
class BasicWorker
{
public:
    template <thread_factory_c<Thread> Factory = StdThreadFactory>
    explicit BasicWorker(Executor & executor, Factory factory = {})
        : m_executor_p(&executor), m_thread(factory(&run_executor, this)) {}

    BasicWorker(const BasicWorker &) = delete;
    BasicWorker & operator=(const BasicWorker &) = delete;
    BasicWorker(BasicWorker &&) = delete;
    BasicWorker & operator=(BasicWorker &&) = delete;

    ~BasicWorker() noexcept { assert(not m_thread.joinable()); }

    void join() noexcept
    {
        // Calling from this worker violates the backend's join precondition.
        if (m_thread.joinable()) {
            m_thread.join();
        }
    }

private:
    static void run_executor(void * state_p) noexcept
    {
        auto & worker = *static_cast<BasicWorker *>(state_p);
        while (worker.m_executor_p->waitOne()) {}
    }

    Executor * m_executor_p;
    Thread m_thread;
};

} // namespace lcf::async
