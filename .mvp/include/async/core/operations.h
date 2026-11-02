#pragma once

#include "concepts.h"

namespace lcf::async {

struct get_schedule_node_t
{
    template <schedule_record_c Record>
    decltype(auto) operator()(Record & record) const noexcept
    {
        return record.getScheduleNode();
    }
};

inline constexpr get_schedule_node_t get_schedule_node{};

struct try_enqueue_t
{
    template <typename Executor, typename Record>
    requires executor_c<Executor, Record>
    EnqueueResult operator()(Executor & executor, Record & record) const noexcept
    {
        return executor.tryEnqueue(record);
    }
};

inline constexpr try_enqueue_t try_enqueue{};

} // namespace lcf::async
