#pragma once

#include <async/core/scheduling/declarations/concepts.h>
#include <async/core/details/WorkSignal.h>
#include <async/core/scheduling/details/dispatch_targets.h>

#include <cassert>
#include <cstdint>
#include <span>

LCF_ASYNC_EXPORT
namespace lcf::async {

template <work_executor_c Executor, work_executor_c OwnerExecutor>
class BasicDispatcher
{
    using ExecutorView = std::span<Executor>;
public:
    ~BasicDispatcher() noexcept = default;
    BasicDispatcher() noexcept = default;
    BasicDispatcher(const BasicDispatcher &) = delete;
    BasicDispatcher(BasicDispatcher &&) = delete;
    BasicDispatcher & operator=(const BasicDispatcher &) = delete;
    BasicDispatcher & operator=(BasicDispatcher &&) = delete;
public:
    void create(ExecutorView executors, OwnerExecutor & owner_executor,
        details::WorkSignal & worker_signal, details::WorkSignal & owner_signal) noexcept
    {
        m_executors = executors;
        m_owner_executor_p = &owner_executor;
        m_worker_signal_p = &worker_signal;
        m_owner_signal_p = &owner_signal;
    }
    void reset() noexcept
    {
        m_executors = {};
        m_owner_executor_p = nullptr;
        m_worker_signal_p = nullptr;
        m_owner_signal_p = nullptr;
    }
    template <work_record_c Record>
    requires record_submitter_c<Executor, Record> and record_submitter_c<OwnerExecutor, Record>
    EnqueueResult tryEnqueue(std::uint32_t target_index, Record & record) noexcept
    {
        if (target_index == details::k_owner_scheduler_index) {
            const auto result = m_owner_executor_p->tryEnqueue(record);
            if (result == EnqueueResult::eAccepted) { m_owner_signal_p->notifyOne(); }
            return result;
        }
        assert(target_index < m_executors.size() and "Scheduler target must belong to the configured Dispatcher");
        const auto result = m_executors[target_index].tryEnqueue(record);
        if (result == EnqueueResult::eAccepted) {
            m_worker_signal_p->notifyOne();
            m_owner_signal_p->notifyOne();
        }
        return result;
    }
private:
    ExecutorView m_executors;
    OwnerExecutor * m_owner_executor_p = nullptr;
    details::WorkSignal * m_worker_signal_p = nullptr;
    details::WorkSignal * m_owner_signal_p = nullptr;
};

} // namespace lcf::async
