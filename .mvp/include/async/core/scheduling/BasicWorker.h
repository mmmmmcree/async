#pragma once

#include "../concepts.h"

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

template <blocking_event_loop_c EventLoop, thread_c Thread = std::thread>
class BasicWorker
{
public:
    template <thread_factory_c<Thread> Factory = StdThreadFactory>
    explicit BasicWorker(EventLoop & loop, Factory factory = {})
        : m_event_loop_p(&loop), m_thread(factory(&run_loop, this)) {}

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
    static void run_loop(void * state_p) noexcept
    {
        auto & worker = *static_cast<BasicWorker *>(state_p);
        while (worker.m_event_loop_p->waitOne()) {}
    }

    EventLoop * m_event_loop_p;
    Thread m_thread;
};

} // namespace lcf::async
