#pragma once

#include "BasicWorker.h"
#include <async/core/worker/details/WorkerControl.h>
#include <async/core/details/containers/FixedCapacityVector.h>
#include <async/core/notify/declarations/concepts.h>

#include <algorithm>
#include <cstdint>
#include <utility>

LCF_ASYNC_EXPORT
namespace lcf::async {

template <worker_progress_c Progress, join_handle_c ThreadHandle, notify::signal_c Signal>
class WorkerPool
{
public:
    using Worker = BasicWorker<Progress, details::WorkerControl<Signal>, ThreadHandle>;
    using WorkerList = details::FixedCapacityVector<Worker>;
public:
    WorkerPool() = delete;
    explicit WorkerPool(Signal & signal) noexcept : m_control(signal) {}
    WorkerPool(Signal & signal, WorkerList workers) noexcept :
        m_control(signal), m_workers(std::move(workers)) {}
    ~WorkerPool() noexcept { this->requestStop(); this->join(); }
    WorkerPool(const WorkerPool &) = delete;
    WorkerPool(WorkerPool &&) = delete;
    WorkerPool & operator=(const WorkerPool &) = delete;
    WorkerPool & operator=(WorkerPool &&) = delete;
public:
    template <typename Factory, thread_launcher_c<ThreadHandle> Launcher>
    void start(Factory make_progress, Launcher launcher)
    {
        if (m_started) { return; }
        m_started = true;
        try {
            for (std::uint64_t index = 0; index < m_workers.capacity(); ++index) {
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
    bool isStopRequested() const noexcept { return m_control.isStopRequested(); }
private:
    details::WorkerControl<Signal> m_control;
    WorkerList m_workers;
    bool m_started = false;
    bool m_running = false;
};

} // namespace lcf::async
