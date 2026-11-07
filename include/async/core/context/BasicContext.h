#pragma once

#include "ContextInfo.h"
#include <async/core/context/declarations/poll_structs.h>
#include <async/core/worker/thread/details/OwnerThreadBinding.h>

#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <stdexcept>
#include <system_error>
#include <thread>

LCF_ASYNC_EXPORT
namespace lcf::async {

template <join_handle_c ThreadHandle = std::thread, std::size_t queue_capacity = 256,
    template <typename, typename, typename, typename> class CpuExecutor = WorkStealingExecutor,
    template <typename> class VictimSelector = RoundRobinVictimSelector,
    typename Notifier = details::ContextTopologyNotifier>
requires (queue_capacity > 0)
class BasicContext
{
    using Topology = details::ContextTopology<queue_capacity, CpuExecutor, VictimSelector, Notifier>;
    using Pool = WorkerPool<typename Topology::Progress, ThreadHandle>;
public:
    BasicContext() = default;
    ~BasicContext() noexcept
    {
        this->requestStop();
        this->joinWorkers();
        assert(m_topology.isIdle() and "Drain accepted operations and return queues before destruction");
    }
    BasicContext(const BasicContext &) = delete;
    BasicContext(BasicContext &&) = delete;
    BasicContext & operator=(const BasicContext &) = delete;
    BasicContext & operator=(BasicContext &&) = delete;
public:
    [[nodiscard]] std::error_code create(const ContextInfo & info) noexcept
    {
        if (m_domain_id != 0) { return std::make_error_code(std::errc::operation_in_progress); }
        if (info.getDomainId() == 0 or info.getDispatcherCount() == 0) {
            return std::make_error_code(std::errc::invalid_argument);
        }
        try {
            const std::size_t thread_count = info.getWorkerCount();
            typename Pool::WorkerList workers{thread_count, {&info.getWorkerMemoryResource()}};
            m_topology.create(info.getDomainId(), info.getSchedulerCount(), info.getWorkerCount(), info.getDispatcherCount(),
                info.getSubmissionPortCount(), &info.getRuntimeMemoryResource());
            std::destroy_at(&m_pool);
            std::construct_at(&m_pool, m_topology.getSignal(), std::move(workers));
        } catch (const std::bad_array_new_length &) {
            return std::make_error_code(std::errc::value_too_large);
        } catch (const std::bad_alloc &) {
            return std::make_error_code(std::errc::not_enough_memory);
        } catch (const std::length_error &) {
            return std::make_error_code(std::errc::value_too_large);
        } catch (const std::system_error & error) {
            return error.code();
        } catch (...) {
            return std::make_error_code(std::errc::io_error);
        }
        m_domain_id = info.getDomainId();
        m_scheduler_count = info.getSchedulerCount();
        m_port_count = info.getSubmissionPortCount();
        return {};
    }
    auto getSchedulerHandle(SchedulerToken token) noexcept
    {
        assert(token.getDomainId() == m_domain_id and token.getIndex() < m_scheduler_count);
        return SchedulerHandle{*this, token};
    }
    auto getEventLoopSchedulerHandle() noexcept
    {
        return SchedulerHandle{*this, m_domain_id, details::k_owner_scheduler_index};
    }
    auto getReturnSchedulerHandle(std::uint32_t port_index) noexcept
    {
        assert(port_index < m_port_count);
        return SchedulerHandle{*this, m_domain_id, m_scheduler_count + port_index};
    }
    auto & getSubmissionPort(std::uint32_t port_index) noexcept
    {
        return m_topology.getSubmissionPort(port_index);
    }
    template <dispatch_record_c Record>
    std::error_code tryEnqueue(std::uint32_t target_index, Record & record) noexcept
    {
        return m_topology.tryEnqueue(target_index, record.getDispatchRequest());
    }
    template <thread_launcher_c<ThreadHandle> Launcher = ThreadLauncher<ThreadHandle>>
    void startWorkers(Launcher launcher = {})
    {
        try {
            m_pool.start([this](std::size_t index) noexcept {
                return m_topology.getWorkerProgress(index);
            }, std::move(launcher));
        } catch (...) {
            this->requestStop();
            throw;
        }
    }
    template <thread_launcher_c<ThreadHandle> Launcher = ThreadLauncher<ThreadHandle>>
    void run(Launcher launcher = {})
    {
        this->startWorkers(std::move(launcher));
        m_owner_binding.enter();
        auto & signal = m_topology.getSignal();
        for (;;) {
            const auto observed = signal.observe();
            if (m_stop_requested) { break; }
            if (m_topology.pollOwner(not m_pool.isRunning()).m_progressed) { continue; }
            if (m_closed) {
                if (m_topology.isIdle()) { break; }
                std::this_thread::yield();
            } else { signal.wait(observed); }
        }
        m_owner_binding.leave();
    }
    bool pollOne() noexcept
    {
        m_owner_binding.enter();
        const bool progressed = m_topology.pollOwner(not m_pool.isRunning()).m_progressed;
        m_owner_binding.leave();
        return progressed;
    }
    PollReport poll(PollBudget budget = {}) noexcept
    {
        using Clock = std::chrono::steady_clock;
        m_owner_binding.enter();
        const auto started = Clock::now();
        PollReport report;
        while (report.m_records < budget.m_max_records) {
            if (Clock::now() - started >= budget.m_max_duration) { break; }
            const auto turn = m_topology.pollOwner(not m_pool.isRunning());
            if (not turn.m_progressed) { break; }
            if (turn.m_executed) { ++report.m_records; }
        }
        report.m_elapsed = Clock::now() - started;
        report.m_count_limit_reached = report.m_records == budget.m_max_records;
        report.m_time_limit_reached = report.m_elapsed >= budget.m_max_duration;
        m_owner_binding.leave();
        return report;
    }
    bool pollReturn(std::uint32_t port_index) noexcept
    {
        return m_topology.pollReturn(port_index);
    }
    void close() noexcept
    {
        m_closed = true;
        m_topology.close();
    }
    void requestStop() noexcept
    {
        m_stop_requested = true;
        m_pool.requestStop();
    }
    void joinWorkers() noexcept
    {
        if (m_closed and not m_stop_requested) {
            while (not m_topology.isIdle()) {
                this->pollOne();
                std::this_thread::yield();
            }
            this->requestStop();
        }
        m_pool.join();
    }
    bool isEmpty() const noexcept { return m_topology.isEmpty(); }
private:
    Topology m_topology;
    Pool m_pool{m_topology.getSignal()};
    details::OwnerThreadBinding m_owner_binding;
    std::uint32_t m_domain_id = 0;
    std::uint32_t m_scheduler_count = 0;
    std::uint32_t m_port_count = 0;
    std::atomic<bool> m_closed{false};
    std::atomic<bool> m_stop_requested{false};
};

using Context = BasicContext<>;

} // namespace lcf::async
