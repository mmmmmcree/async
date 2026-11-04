#pragma once

#include "ContextInfo.h"
#include <async/core/context/declarations/poll_structs.h>
#include <async/core/context/details/ContextDriver.h>
#include <async/core/details/containers/FixedCapacityVector.h>
#include <async/core/scheduling/BasicExecutor.h>
#include <async/core/scheduling/BasicDispatcher.h>
#include <async/core/scheduling/SchedulerHandle.h>

#include <cassert>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <memory_resource>
#include <new>
#include <span>
#include <stdexcept>
#include <system_error>
#include <thread>
#include <type_traits>
#include <utility>

LCF_ASYNC_EXPORT
namespace lcf::async {

template <multi_worker_executor_c Executor = BasicExecutor<>,
    join_handle_c ThreadHandle = std::thread, work_executor_c LoopExecutor = BasicExecutor<>>
class BasicContext
{
    using Dispatcher = BasicDispatcher<Executor, LoopExecutor>;
    using Pool = WorkerPool<Executor, ThreadHandle>;
    using Worker = typename Pool::Worker;
    using EventLoop = BasicEventLoop<LoopExecutor>;
    using Driver = details::ContextDriver<Executor, ThreadHandle, LoopExecutor>;
    using ExecutorList = details::FixedCapacityVector<Executor>;
public:
    ~BasicContext() noexcept
    {
        if (m_domain_id == 0) { return; }
        this->requestStop();
        this->joinWorkers();
        assert(this->isEmpty() and "Pending records must complete before Context destruction");
    }
    BasicContext() noexcept(std::is_nothrow_default_constructible_v<LoopExecutor>) = default;
    BasicContext(const BasicContext &) = delete;
    BasicContext(BasicContext &&) = delete;
    BasicContext & operator=(const BasicContext &) = delete;
    BasicContext & operator=(BasicContext &&) = delete;
public:
    [[nodiscard]] std::error_code create(const ContextInfo & context_info) noexcept
    requires std::default_initializable<Executor> and std::default_initializable<LoopExecutor>
    {
        if (m_domain_id != 0) { return std::make_error_code(std::errc::operation_in_progress); }
        if (context_info.getDomainId() == 0) { return std::make_error_code(std::errc::invalid_argument); }
        if (auto error = this->createResources(context_info)) {
            m_event_loop.reset();
            m_dispatcher.reset();
            m_worker_pool.reset();
            m_executors = ExecutorList{};
            return error;
        }
        m_domain_id = context_info.getDomainId();
        return {};
    }
    SchedulerHandle<Dispatcher> getSchedulerHandle(SchedulerToken token) const noexcept
    {
        assert(token.getDomainId() == m_domain_id and "Scheduler token belongs to a different configuration");
        return SchedulerHandle<Dispatcher>{m_dispatcher, token};
    }
    auto getEventLoopSchedulerHandle() const noexcept
    {
        return SchedulerHandle{m_dispatcher};
    }
    template <thread_launcher_c<ThreadHandle> Launcher = ThreadLauncher<ThreadHandle>>
    void startWorkers(Launcher thread_launcher = {})
    {
        try {
            m_worker_pool.start(std::move(thread_launcher));
        } catch (...) {
            m_event_loop.requestStop();
            throw;
        }
    }
    template <thread_launcher_c<ThreadHandle> Launcher = ThreadLauncher<ThreadHandle>>
    void run(Launcher thread_launcher = {})
    {
        this->startWorkers(std::move(thread_launcher));
        m_driver.run();
    }
    bool pollOne() noexcept { return m_driver.pollOne(); }
    PollReport poll(PollBudget budget = {}) noexcept { return m_driver.poll(budget); }
    void requestStop() noexcept
    {
        m_worker_pool.requestStop();
        m_event_loop.requestStop();
    }
    void close() noexcept
    {
        m_worker_pool.close();
        m_event_loop.close();
    }
    void joinWorkers() noexcept { m_worker_pool.join(); }
    bool isEmpty() const noexcept
    {
        return m_event_loop.isEmpty() and m_worker_pool.isEmpty();
    }
private:
    std::error_code createResources(const ContextInfo & context_info) noexcept
    requires std::default_initializable<Executor> and std::default_initializable<LoopExecutor>
    {
        try {
            const auto allocators = context_info.getAllocators();
            m_executors = ExecutorList{context_info.getSchedulerCount(),
                std::pmr::polymorphic_allocator<Executor>{allocators.m_executors}};
            for (std::size_t index = 0; index < context_info.getSchedulerCount(); ++index) {
                m_executors.emplace_back();
            }
            m_worker_pool.create(std::span{m_executors}, m_worker_signal, context_info.getWorkerCount(),
                std::pmr::polymorphic_allocator<Worker>{allocators.m_workers});
            m_event_loop.create(m_loop_executor, m_loop_signal);
            m_dispatcher.create(std::span{m_executors}, m_loop_executor, m_worker_signal, m_loop_signal);
            m_driver.create(m_event_loop, m_worker_pool);
            return {};
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
    }
private:
    details::WorkSignal m_worker_signal;
    details::WorkSignal m_loop_signal;
    ExecutorList m_executors;
    LoopExecutor m_loop_executor;
    mutable Dispatcher m_dispatcher;
    EventLoop m_event_loop;
    Pool m_worker_pool;
    Driver m_driver;
    std::uint32_t m_domain_id = 0;
};

using Context = BasicContext<>;

} // namespace lcf::async
