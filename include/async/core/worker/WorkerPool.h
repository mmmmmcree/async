#pragma once

#include <async/config/macros.h>
#include <async/core/worker/BasicWorker.h>
#include <async/core/details/containers/FixedCapacityVector.h>

LCF_ASYNC_EXPORT
namespace lcf::async {

template <work_executor_c ExecutionPath, typename Coordinator, join_handle_c ThreadHandle>
class WorkerPool
{
    using Worker = BasicWorker<ExecutionPath, Coordinator, ThreadHandle>;
public:
    ~WorkerPool() noexcept { this->join(); }
    WorkerPool() noexcept = default;
    WorkerPool(const WorkerPool &) = delete;
    WorkerPool(WorkerPool &&) = delete;
    WorkerPool & operator=(const WorkerPool &) = delete;
    WorkerPool & operator=(WorkerPool &&) = delete;
public:
    template <typename Storage>
    void prepareStorage(std::size_t count, Storage & storage) { storage.reserve(m_workers, count); }
    template <typename Factory>
    void prepare(std::size_t count, Coordinator & coordinator, std::pmr::memory_resource & resource, Factory make_path)
    {
        m_workers = {count, {&resource}};
        for (std::size_t index = 0; index < count; ++index) { m_workers.emplace_back(make_path(index), coordinator); }
    }
    template <thread_launcher_c<ThreadHandle> Launcher>
    void start(Coordinator & coordinator, Launcher launcher)
    {
        try { for (Worker & worker : m_workers) { worker.start(launcher); } }
        catch (...) { coordinator.requestStop(); this->join(); throw; }
    }
    void join() noexcept { for (Worker & worker : m_workers) { worker.join(); } }
private:
    details::FixedCapacityVector<Worker> m_workers;
};

} // namespace lcf::async
