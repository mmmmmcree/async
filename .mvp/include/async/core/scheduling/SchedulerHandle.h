#pragma once

#include "LogicalQueue.h"
#include <async/core/scheduling/details/exec_schedule_adapters.h>

namespace lcf::async {

template <typename Executor>
class SchedulerHandle
{
public:
    using scheduler_concept = protocols::scheduler_t;
public:
    ~SchedulerHandle() noexcept = default;
    SchedulerHandle() = delete;
    explicit SchedulerHandle(const LogicalQueue<Executor> & queue) noexcept : m_queue_p(&queue) {}
    SchedulerHandle(const LogicalQueue<Executor> &&) = delete;
    SchedulerHandle(const SchedulerHandle &) noexcept = default;
    SchedulerHandle(SchedulerHandle &&) noexcept = default;
    SchedulerHandle & operator=(const SchedulerHandle &) noexcept = default;
    SchedulerHandle & operator=(SchedulerHandle &&) noexcept = default;
public:
    bool operator==(const SchedulerHandle &) const noexcept = default;
public:
    auto schedule() const noexcept;
    template <typename Record>
    requires record_submitter_c<const LogicalQueue<Executor>, Record>
    EnqueueResult tryEnqueue(Record & record) const noexcept { return m_queue_p->tryEnqueue(record); }
private:
    const LogicalQueue<Executor> * m_queue_p;
};

template <typename Executor>
auto SchedulerHandle<Executor>::schedule() const noexcept
{
    return details::ScheduleSender<SchedulerHandle>{*this};
}

} // namespace lcf::async
