#pragma once

#include "ContextInfo.h"
#include "../operations.h"
#include "../containers/details/FixedCapacityVector.h"
#include "../scheduling/Poll.h"
#include "../scheduling/BasicEventLoop.h"
#include "../scheduling/BasicWorker.h"
#include "../scheduling/LogicalQueue.h"
#include "../scheduling/SchedulerHandle.h"

#include <cassert>
#include <chrono>
#include <concepts>
#include <exception>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory_resource>
#include <stdexcept>
#include <thread>
#include <utility>

namespace lcf::async {

namespace details {

template <event_loop_c EventLoop>
PollReport poll_event_loop(EventLoop & event_loop, PollBudget budget = {}) noexcept
{
    using Clock = std::chrono::steady_clock;
    const auto started = Clock::now();
    const bool finite_time = budget.m_max_duration != Clock::duration::max();
    PollReport report;
    while (report.m_records < budget.m_max_records) {
        if (finite_time && Clock::now() - started >= budget.m_max_duration) { break; }
        if (not event_loop.pollOne()) { break; }
        ++report.m_records;
    }
    report.m_elapsed = Clock::now() - started;
    report.m_count_limit_reached = report.m_records == budget.m_max_records;
    report.m_time_limit_reached = finite_time && report.m_elapsed >= budget.m_max_duration;
    return report;
}

} // namespace details

template <blocking_event_loop_c EventLoop = BasicEventLoop<>, thread_c Thread = std::thread>
class BasicContext
{
public:
    using Worker = BasicWorker<EventLoop, Thread>;

    template <event_loop_factory_c<EventLoop> LoopFactory = DefaultEventLoopFactory<EventLoop>,
        thread_factory_c<Thread> ThreadFactory = StdThreadFactory>
    explicit BasicContext(ContextInfo context_info, ContextAllocators allocators,
        LoopFactory loop_factory = {}, ThreadFactory thread_factory = {})
        : m_domain_id(validate_info(context_info)),
          m_control_loop(loop_factory.makeControlLoop()),
          m_worker_loops(context_info.getWorkerCount(),
              std::pmr::polymorphic_allocator<EventLoop>{allocators.m_worker_loops}),
          m_workers(context_info.getWorkerCount(),
              std::pmr::polymorphic_allocator<Worker>{allocators.m_workers}),
          m_queues(context_info.getQueueBindings().size(),
              std::pmr::polymorphic_allocator<LogicalQueue<EventLoop>>{allocators.m_routes})
    {
        for (WorkerIndex index = 0; index < context_info.getWorkerCount(); ++index) {
            m_worker_loops.emplaceWith([&] { return loop_factory.makeWorkerLoop(index); });
        }
        for (const auto & binding : context_info.getQueueBindings()) {
            auto & loop = binding.m_on_worker
                ? m_worker_loops[binding.m_worker_index] : m_control_loop;
            m_queues.emplaceBack(loop);
        }
        try {
            for (auto & loop : m_worker_loops.values()) {
                m_workers.emplaceBack(loop, thread_factory);
            }
        } catch (...) {
            this->close();
            this->joinWorkers();
            throw;
        }
    }

    BasicContext(const BasicContext &) = delete;
    BasicContext & operator=(const BasicContext &) = delete;
    ~BasicContext() noexcept { assert(this->empty()); }

    static constexpr ContextStorageRequirements storageRequirements(
        WorkerIndex worker_count, std::size_t queue_count)
    {
        return {storage_requirement<EventLoop>(worker_count),
            storage_requirement<Worker>(worker_count),
            storage_requirement<LogicalQueue<EventLoop>>(queue_count)};
    }

    static ContextStorageRequirements storageRequirements(const ContextInfo & info)
    {
        return storageRequirements(info.getWorkerCount(), info.getQueueBindings().size());
    }

    SchedulerHandle<EventLoop> getSchedulerHandle(LogicalQueueToken token) const noexcept
    {
        const auto * queue_p = this->findLogicalQueue(token);
        // A token from another configuration violates this accessor's contract.
        if (not queue_p) { std::terminate(); }
        return SchedulerHandle<EventLoop>{*queue_p};
    }

    const LogicalQueue<EventLoop> * findLogicalQueue(LogicalQueueToken token) const noexcept
    {
        if (token.getDomainId() != m_domain_id || token.getIndex() >= m_queues.size()) {
            return nullptr;
        }
        return &m_queues[token.getIndex()];
    }

    bool pollOne() noexcept { return m_control_loop.pollOne(); }
    PollReport poll(PollBudget budget = {}) noexcept
    {
        return details::poll_event_loop(m_control_loop, budget);
    }
    void run() noexcept requires stoppable_event_loop_c<EventLoop>
    {
        while (m_control_loop.waitOne()) {}
    }
    void requestStop() noexcept requires stoppable_event_loop_c<EventLoop>
    {
        m_control_loop.requestStop();
    }
    void close() noexcept
    {
        m_control_loop.close();
        for (auto & loop : m_worker_loops.values()) { loop.close(); }
    }
    void joinWorkers() noexcept
    {
        for (auto & worker : m_workers.values()) { worker.join(); }
    }
    bool empty() const noexcept
    {
        if (not m_control_loop.empty()) { return false; }
        for (const auto & loop : m_worker_loops.values()) {
            if (not loop.empty()) { return false; }
        }
        return true;
    }

private:
    static std::uint64_t validate_info(const ContextInfo & info)
    {
        if (info.getDomainId() == 0 || info.getQueueBindings().empty()) {
            throw std::invalid_argument{"Context requires an unconsumed configuration with routes"};
        }
        return info.getDomainId();
    }

    template <std::destructible Value>
    static constexpr StorageRequirement storage_requirement(std::size_t count)
    {
        if (count > std::numeric_limits<std::size_t>::max() / sizeof(Value)) {
            throw std::length_error{"Context storage size overflow"};
        }
        return {count * sizeof(Value), alignof(Value)};
    }

    std::uint64_t m_domain_id;
    EventLoop m_control_loop;
    details::FixedCapacityVector<EventLoop> m_worker_loops;
    details::FixedCapacityVector<Worker> m_workers;
    details::FixedCapacityVector<LogicalQueue<EventLoop>> m_queues;
};

using Context = BasicContext<>;

} // namespace lcf::async
