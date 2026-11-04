#pragma once

#include <async/core/scheduling/declarations/concepts.h>
#include <async/core/scheduling/details/dispatch_targets.h>

#include <cassert>
#include <cstdint>
#include <span>

LCF_ASYNC_EXPORT
namespace lcf::async {

template <work_executor_c Executor, work_executor_c OwnerExecutor, work_notifier_c Notifier>
class BasicDispatcher
{
    using ExecutorSpan = std::span<Executor>;
public:
    ~BasicDispatcher() noexcept = default;
    BasicDispatcher() = delete;
    BasicDispatcher(ExecutorSpan executors, OwnerExecutor & owner_executor,
        Notifier & worker_notifier, Notifier & owner_notifier) noexcept :
        m_executors(executors), m_owner_executor_p(&owner_executor),
        m_worker_notifier_p(&worker_notifier), m_owner_notifier_p(&owner_notifier) {}
    BasicDispatcher(const BasicDispatcher &) = delete;
    BasicDispatcher(BasicDispatcher &&) noexcept = default;
    BasicDispatcher & operator=(const BasicDispatcher &) = delete;
    BasicDispatcher & operator=(BasicDispatcher &&) noexcept = default;
public:
    template <typename Record>
    requires record_submitter_c<Executor, Record> and record_submitter_c<OwnerExecutor, Record>
    EnqueueResult tryEnqueue(std::uint32_t target_index, Record & record) noexcept
    {
        if (target_index == details::k_owner_scheduler_index) {
            const auto result = m_owner_executor_p->tryEnqueue(record);
            if (result == EnqueueResult::eAccepted) { m_owner_notifier_p->notifyOne(); }
            return result;
        }
        assert(target_index < m_executors.size() and "Scheduler target must belong to the configured Dispatcher");
        const auto result = m_executors[target_index].tryEnqueue(record);
        if (result == EnqueueResult::eAccepted) {
            m_worker_notifier_p->notifyOne();
            m_owner_notifier_p->notifyOne();
        }
        return result;
    }
private:
    ExecutorSpan m_executors;
    OwnerExecutor * m_owner_executor_p;
    Notifier * m_worker_notifier_p;
    Notifier * m_owner_notifier_p;
};

} // namespace lcf::async
