#pragma once

#include <async/config/macros.h>
#include <async/core/worker/BasicWorker.h>
#include <async/core/details/containers/FixedCapacityList.h>

LCF_ASYNC_EXPORT
namespace lcf::async {

template <work_executor_c Executor, worker_coordinator_c Coordinator, join_handle_c ThreadHandle>
class WorkerPool
{
    using Worker = BasicWorker<Executor, Coordinator, ThreadHandle>;
    using WorkerList = details::FixedCapacityList<Worker>;
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
    void prepare(std::size_t count, Coordinator & coordinator, std::pmr::memory_resource & resource, Factory make_executor)
    {
        m_workers = {count, {&resource}};
        for (std::size_t index = 0; index < count; ++index) { m_workers.emplace_back(make_executor(index), coordinator); }
    }
    template <thread_launcher_c<ThreadHandle> Launcher>
    std::error_code start(Coordinator & coordinator, Launcher & launcher) noexcept
    {
        for (Worker & worker : m_workers) {
            if (auto ec = worker.start(launcher)) {
                coordinator.requestStop();
                this->join();
                return ec;
            }
        }
        return {};
    }
    void join() noexcept { for (Worker & worker : m_workers) { worker.join(); } }
private:
    WorkerList m_workers;
};

} // namespace lcf::async
