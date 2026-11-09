#pragma once

#include <async/core/executable/declarations/concepts.h>
#include <async/core/scheduling/dispatcher/DispatchRequest.h>
#include <async/core/scheduling/declarations/concepts.h>
#include <async/execution/operations.h>
#include <async/execution/queries.h>

#include <concepts>
#include <system_error>
#include <type_traits>
#include <utility>

namespace lcf::async::details {

using ScheduleCompletions = protocols::completion_signatures<
    exec::set_value_t(), exec::set_error_t(std::error_code), exec::set_stopped_t()>;

template <std::copyable Handle, callback_executable_c Executable, receiver_of_c<ScheduleCompletions> Receiver>
class ScheduleOperation
{
    using DispatchRequest = ::lcf::async::DispatchRequest<Executable>;
public:
    using operation_state_concept = protocols::operation_state_t;
private:
    static void cancel(void * state_p) noexcept
    {
        ScheduleOperation & operation = *static_cast<ScheduleOperation *>(state_p);
        exec::set_stopped(std::move(operation.m_receiver));
    }
    static void execute(void * state_p) noexcept
    {
        ScheduleOperation & operation = *static_cast<ScheduleOperation *>(state_p);
        const auto & environment = exec::get_env(operation.m_receiver);
        if (exec::get_stop_token(environment).stop_requested()) {
            cancel(state_p);
            return;
        }
        exec::set_value(std::move(operation.m_receiver));
    }
public:
    ~ScheduleOperation() noexcept = default;
    ScheduleOperation() = delete;
    ScheduleOperation(Handle handle, Receiver receiver) noexcept(
        std::is_nothrow_copy_constructible_v<Handle> and std::is_nothrow_move_constructible_v<Receiver>) :
        m_handle(handle), m_receiver(std::move(receiver)),
        m_executable(this, &execute, &cancel), m_request {m_executable} {}
    ScheduleOperation(const ScheduleOperation &) = delete;
    ScheduleOperation & operator=(const ScheduleOperation &) = delete;
    ScheduleOperation(ScheduleOperation &&) = delete;
    ScheduleOperation & operator=(ScheduleOperation &&) = delete;
public:
    void start() & noexcept
    requires record_submitter_c<Handle, DispatchRequest>
    {
        const std::error_code ec = m_handle.submit(m_request);
        if (not ec) { return; }
        if (ec == std::errc::operation_canceled) { exec::set_stopped(std::move(m_receiver)); }
        else { exec::set_error(std::move(m_receiver), ec); }
    }
private:
    Handle m_handle;
    Receiver m_receiver;
    Executable m_executable;
    DispatchRequest m_request;
};

template <std::copyable Handle, callback_executable_c Executable>
struct ScheduleSender
{
    using DispatchRequest = ::lcf::async::DispatchRequest<Executable>;
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
    requires record_submitter_c<Handle, DispatchRequest>
    ScheduleOperation<Handle, Executable, Receiver> connect(Receiver receiver) const
    {
        return ScheduleOperation<Handle, Executable, Receiver> {m_scheduler, std::move(receiver)};
    }
private:
    Handle m_scheduler;
};

} // namespace lcf::async::details
