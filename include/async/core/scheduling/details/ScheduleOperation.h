#pragma once
#include <async/core/scheduling/declarations/concepts.h>
#include <async/core/details/concepts/constructible.h>
#include <async/execution/operations.h>
#include <async/execution/queries.h>
#include <utility>
namespace lcf::async::details {
using ScheduleCompletions = protocols::completion_signatures<
    exec::set_value_t(), exec::set_error_t(std::error_code), exec::set_stopped_t()>;
template <typename Handle, receiver_of_c<ScheduleCompletions> Receiver>
class ScheduleOperation
{
    using Executable = typename Handle::executable_t;
    using Record = typename Handle::record_t;
    static void cancel(void * pointer) noexcept
    {
        ScheduleOperation & operation = *static_cast<ScheduleOperation *>(pointer);
        exec::set_stopped(std::move(operation.m_receiver));
    }
    static void execute(void * pointer) noexcept
    {
        ScheduleOperation & operation = *static_cast<ScheduleOperation *>(pointer);
        if (exec::get_stop_token(exec::get_env(operation.m_receiver)).stop_requested()) { cancel(pointer); return; }
        exec::set_value(std::move(operation.m_receiver));
    }
public:
    using operation_state_concept = protocols::operation_state_t;
public:
    ~ScheduleOperation() noexcept = default;
    ScheduleOperation() = delete;
    ScheduleOperation(Handle handle, Receiver receiver) noexcept(all_nothrow_move_constructible<Handle, Receiver>) :
        m_handle(std::move(handle)), m_receiver(std::move(receiver)),
        m_executable(this, &execute, &cancel), m_record(m_executable) {}
    ScheduleOperation(const ScheduleOperation &) = delete;
    ScheduleOperation(ScheduleOperation &&) = delete;
    ScheduleOperation & operator=(const ScheduleOperation &) = delete;
    ScheduleOperation & operator=(ScheduleOperation &&) = delete;
    void start() & noexcept
    {
        const std::error_code ec = m_handle.submit(m_record);
        if (not ec) { return; }
        if (ec == std::errc::operation_canceled) { cancel(this); }
        else { exec::set_error(std::move(m_receiver), ec); }
    }
private:
    Handle m_handle;
    Receiver m_receiver;
    Executable m_executable;
    Record m_record;
};
}
