#pragma once

#include "BasicWorker.h"
#include <async/core/worker/details/WorkerControl.h>
#include <async/core/details/containers/FixedCapacityVector.h>

#include <algorithm>
#include <thread>
#include <utility>

LCF_ASYNC_EXPORT
namespace lcf::async {

template <worker_progress_c Progress, join_handle_c ThreadHandle = std::thread>
class WorkerPool
{
public:
    using Worker = BasicWorker<Progress, details::WorkerControl, ThreadHandle>;
    using WorkerList = details::FixedCapacityVector<Worker>;
public:
    explicit WorkerPool(details::WorkSignal & signal) noexcept : m_control(signal) {}
    WorkerPool(details::WorkSignal & signal, WorkerList workers) noexcept :
        m_control(signal), m_workers(std::move(workers)) {}
    ~WorkerPool() noexcept { this->requestStop(); this->join(); }
    WorkerPool(const WorkerPool &) = delete;
    WorkerPool(WorkerPool &&) = delete;
    WorkerPool & operator=(const WorkerPool &) = delete;
    WorkerPool & operator=(WorkerPool &&) = delete;
public:
    template <typename Factory, thread_launcher_c<ThreadHandle> Launcher = ThreadLauncher<ThreadHandle>>
    void start(Factory make_progress, Launcher launcher = {})
    {
        if (m_started) { return; }
        m_started = true;
        try {
            for (std::size_t index = 0; index < m_workers.capacity(); ++index) {
                m_workers.emplace_back(launcher, make_progress(index), m_control);
            }
        } catch (...) {
            this->requestStop();
            this->join();
            throw;
        }
        m_running = std::ranges::any_of(m_workers, &Worker::isJoinable);
    }
    void requestStop() noexcept { m_control.requestStop(); }
    // Only use after producers and upstream stages can no longer add work.
    void close() noexcept { m_control.close(); }
    void join() noexcept
    {
        std::ranges::for_each(m_workers, &Worker::join);
        m_running = false;
    }
    bool isRunning() const noexcept { return m_running; }
private:
    details::WorkerControl m_control;
    WorkerList m_workers;
    bool m_started = false;
    bool m_running = false;
};

} // namespace lcf::async
