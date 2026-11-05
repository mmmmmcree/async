#pragma once

#include <async/core/queue/intrusive/ExecutionNode.h>
#include <async/core/scheduling/declarations/dispatch_structs.h>
#include <async/core/scheduling/declarations/concepts.h>
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
public:
    ~ScheduleOperation() noexcept = default;
    ScheduleOperation() = delete;
    ScheduleOperation(Handle handle, Receiver receiver) noexcept(
        std::is_nothrow_copy_constructible_v<Handle> and std::is_nothrow_move_constructible_v<Receiver>) :
        m_handle(handle), m_receiver(std::move(receiver)), m_node(this, &execute), m_request{&m_node} {}
    ScheduleOperation(const ScheduleOperation &) = delete;
    ScheduleOperation & operator=(const ScheduleOperation &) = delete;
    ScheduleOperation(ScheduleOperation &&) = delete;
    ScheduleOperation & operator=(ScheduleOperation &&) = delete;
public:
    queue::intrusive::ExecutionNode & getExecutable() noexcept { return m_node; }
    DispatchRequest & getDispatchRequest() noexcept { return m_request; }
    void start() & noexcept
    requires record_submitter_c<Handle, ScheduleOperation>
    {
        if (m_handle.tryEnqueue(*this)) { exec::set_stopped(std::move(m_receiver)); }
    }
private:
    Handle m_handle;
    Receiver m_receiver;
    queue::intrusive::ExecutionNode m_node;
    DispatchRequest m_request;
};

template <std::copyable Handle>
struct ScheduleSender
{
    using sender_concept = protocols::sender_t;
    using completion_signatures = ScheduleCompletions;
    struct Environment
    {
        ~Environment() noexcept = default;
        Environment() = delete;
        explicit Environment(Handle scheduler) noexcept(std::is_nothrow_move_constructible_v<Handle>)
            : m_scheduler(std::move(scheduler)) {}
        Environment(const Environment &) = default;
        Environment(Environment &&) = default;
        Environment & operator=(const Environment &) = default;
        Environment & operator=(Environment &&) = default;

        Handle query(exec::get_completion_scheduler_t<exec::set_value_t>) const noexcept
        {
            return m_scheduler;
        }

        Handle m_scheduler;
    };
public:
    ~ScheduleSender() noexcept = default;
    ScheduleSender() = delete;
    explicit ScheduleSender(Handle scheduler) noexcept(std::is_nothrow_move_constructible_v<Handle>) :
        m_scheduler(std::move(scheduler)) {}
    ScheduleSender(const ScheduleSender &) = default;
    ScheduleSender(ScheduleSender &&) = default;
    ScheduleSender & operator=(const ScheduleSender &) = default;
    ScheduleSender & operator=(ScheduleSender &&) = default;
public:
    Environment get_env() const noexcept { return Environment{m_scheduler}; }
    template <receiver_of_c<ScheduleCompletions> Receiver>
    requires record_submitter_c<Handle, ScheduleOperation<Handle, Receiver>>
    auto connect(Receiver receiver) const { return ScheduleOperation<Handle, Receiver>{m_scheduler, std::move(receiver)}; }
private:
    Handle m_scheduler;
};

} // namespace lcf::async::details
