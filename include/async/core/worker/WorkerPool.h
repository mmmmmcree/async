#pragma once

#include "BasicWorker.h"
#include <async/core/worker/selectors/RoundRobinSelector.h>
#include "WorkerDriver.h"
#include <async/core/worker/details/WorkerControl.h>
#include <async/core/details/containers/FixedCapacityVector.h>

#include <algorithm>
#include <cstdint>
#include <memory_resource>
#include <span>
#include <thread>

LCF_ASYNC_EXPORT
namespace lcf::async {

template <multi_worker_executor_c Executor, join_handle_c ThreadHandle = std::thread>
class WorkerPool
{
    using Selector = RoundRobinSelector<Executor>;
public:
    using Driver = WorkerDriver<Selector, details::WorkerControl>;
    using Worker = BasicWorker<Driver, ThreadHandle>;
private:
    using WorkerList = details::FixedCapacityVector<Worker>;
    using ExecutorView = std::span<Executor>;
public:
    ~WorkerPool() noexcept
    {
        if (m_workers.empty()) { return; }
        this->requestStop();
        this->join();
    }
    WorkerPool() noexcept = default;
    WorkerPool(const WorkerPool &) = delete;
    WorkerPool(WorkerPool &&) = delete;
    WorkerPool & operator=(const WorkerPool &) = delete;
    WorkerPool & operator=(WorkerPool &&) = delete;
public:
    void create(ExecutorView executors, details::WorkSignal & signal, std::uint32_t worker_count,
        std::pmr::polymorphic_allocator<Worker> allocator)
    {
        m_executors = executors;
        m_control.create(signal);
        m_workers = WorkerList{worker_count, allocator};
    }
    void reset() noexcept
    {
        m_workers = WorkerList{};
        m_executors = {};
        m_started = false;
        m_running = false;
    }
    template <thread_launcher_c<ThreadHandle> Launcher = ThreadLauncher<ThreadHandle>>
    void start(Launcher thread_launcher = {})
    {
        if (m_started) { return; }
        m_started = true;
        try {
            for (std::size_t index = 0; index < m_workers.capacity(); ++index) {
                m_workers.emplace_back(thread_launcher, Selector{m_executors, index}, m_control);
            }
        } catch (...) {
            this->requestStop();
            this->join();
            throw;
        }
        m_running = std::ranges::any_of(m_workers, &Worker::isJoinable);
    }
    void requestStop() noexcept { m_control.requestStop(); }
    void close() noexcept
    {
        std::ranges::for_each(m_executors, &Executor::close);
        m_control.close();
    }
    void join() noexcept
    {
        std::ranges::for_each(m_workers, &Worker::join);
        m_running = false;
    }
    bool isRunning() const noexcept { return m_running; }
    bool isEmpty() const noexcept { return std::ranges::all_of(m_executors, &Executor::isEmpty); }
private:
    ExecutorView m_executors;
    details::WorkerControl m_control;
    WorkerList m_workers;
    bool m_started = false;
    bool m_running = false;
};

} // namespace lcf::async
