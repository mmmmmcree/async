#pragma once

#include "BasicWorker.h"
#include <async/core/worker/selectors/RoundRobinSelector.h>
#include <async/core/worker/details/WorkerControl.h>
#include <async/core/details/containers/FixedCapacityVector.h>

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <memory_resource>
#include <span>
#include <thread>
#include <utility>

LCF_ASYNC_EXPORT
namespace lcf::async {

template <multi_worker_executor_c Executor, join_handle_c ThreadHandle = std::thread>
class WorkerPool
{
    using Selector = RoundRobinSelector<Executor>;
public:
    using Worker = BasicWorker<Selector, details::WorkerControl, ThreadHandle>;
    using WorkerList = details::FixedCapacityVector<Worker>;
private:
    using ExecutorSpan = std::span<Executor>;
public:
    ~WorkerPool() noexcept
    {
        if (m_workers.empty()) { return; }
        this->requestStop();
        this->join();
    }
    WorkerPool() = delete;
    explicit WorkerPool(details::WorkSignal & signal) noexcept : m_control(signal) {}
    WorkerPool(ExecutorSpan executors, details::WorkSignal & signal, WorkerList workers) noexcept :
        m_executors(executors), m_control(signal), m_workers(std::move(workers))
    {
        assert(m_workers.empty() and "Worker storage must be empty before binding its control");
    }
    WorkerPool(ExecutorSpan executors, details::WorkSignal & signal, std::uint32_t worker_count,
        std::pmr::polymorphic_allocator<Worker> allocator) :
        WorkerPool(executors, signal, WorkerList{worker_count, allocator}) {}
    WorkerPool(const WorkerPool &) = delete;
    WorkerPool(WorkerPool &&) = delete;
    WorkerPool & operator=(const WorkerPool &) = delete;
    WorkerPool & operator=(WorkerPool &&) = delete;
public:
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
    ExecutorSpan m_executors;
    details::WorkerControl m_control;
    WorkerList m_workers;
    bool m_started = false;
    bool m_running = false;
};

} // namespace lcf::async
