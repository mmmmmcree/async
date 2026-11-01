#pragma once

#include <async/core/operations.h>
#include <async/core/scheduling/ScheduleNode.h>
#include <async/core/execution_concepts.h>
#include <async/execution/operations.h>
#include <async/execution/queries.h>

#include <concepts>
#include <type_traits>
#include <utility>

namespace lcf::async::details {

using ScheduleCompletions = protocols::completion_signatures<
    exec::set_value_t(), exec::set_stopped_t()>;

template <std::copyable Handle, receiver_of_c<ScheduleCompletions> Receiver>
class ScheduleOperation
{
public:
    using operation_state_concept = protocols::operation_state_t;

    ScheduleOperation(Handle handle, Receiver receiver)
        noexcept(std::is_nothrow_copy_constructible_v<Handle>
            && std::is_nothrow_move_constructible_v<Receiver>)
        : m_handle(handle), m_receiver(std::move(receiver)), m_node(this, &execute) {}

    ScheduleOperation(const ScheduleOperation &) = delete;
    ScheduleOperation & operator=(const ScheduleOperation &) = delete;
    ScheduleOperation(ScheduleOperation &&) = delete;
    ScheduleOperation & operator=(ScheduleOperation &&) = delete;

    ScheduleNode & getScheduleNode() noexcept { return m_node; }

    void start() & noexcept
        requires executor_c<Handle, ScheduleOperation>
    {
        if (try_enqueue(m_handle, *this) == EnqueueResult::eAccepted) {
            return;
        }
        exec::set_stopped(std::move(m_receiver));
    }

private:
    static void execute(void * state_p) noexcept
    {
        auto & operation = *static_cast<ScheduleOperation *>(state_p);
        const auto & environment = exec::get_env(operation.m_receiver);
        if (exec::get_stop_token(environment).stop_requested()) {
            exec::set_stopped(std::move(operation.m_receiver));
            return;
        }
        exec::set_value(std::move(operation.m_receiver));
    }

    Handle m_handle;
    Receiver m_receiver;
    ScheduleNode m_node;
};

template <std::copyable Handle>
struct ScheduleSender
{
    using sender_concept = protocols::sender_t;
    using completion_signatures = ScheduleCompletions;

    struct Environment
    {
        Handle m_scheduler;

        Handle query(exec::get_completion_scheduler_t<exec::set_value_t>) const noexcept
        {
            return m_scheduler;
        }
    };

    Environment get_env() const noexcept { return {m_scheduler}; }

    template <receiver_of_c<ScheduleCompletions> Receiver>
        requires executor_c<Handle, ScheduleOperation<Handle, Receiver>>
    auto connect(Receiver receiver) const
    {
        return ScheduleOperation<Handle, Receiver>{m_scheduler, std::move(receiver)};
    }

    Handle m_scheduler;
};

} // namespace lcf::async::details
