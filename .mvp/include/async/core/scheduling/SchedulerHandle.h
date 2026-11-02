#pragma once

#include "LogicalQueue.h"
#include <async/core/scheduling/details/exec_schedule_adapters.h>

namespace lcf::async {

template <typename Executor>
class SchedulerHandle
{
public:
    using scheduler_concept = protocols::scheduler_t;

    explicit SchedulerHandle(const LogicalQueue<Executor> & queue) noexcept
        : m_queue_p(&queue) {}
    SchedulerHandle(const LogicalQueue<Executor> &&) = delete;

    auto schedule() const noexcept;

    template <typename Record>
        requires executor_c<const LogicalQueue<Executor>, Record>
    EnqueueResult tryEnqueue(Record & record) const noexcept
    {
        return try_enqueue(*m_queue_p, record);
    }

    bool operator==(const SchedulerHandle &) const noexcept = default;

private:
    const LogicalQueue<Executor> * m_queue_p;
};

template <typename Executor>
auto SchedulerHandle<Executor>::schedule() const noexcept
{
    return details::ScheduleSender<SchedulerHandle>{*this};
}

} // namespace lcf::async
