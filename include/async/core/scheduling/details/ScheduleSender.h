#pragma once

#include <async/core/scheduling/details/ScheduleOperation.h>

namespace lcf::async::details {

template <typename Handle>
class ScheduleSender
{
    class Environment
    {
    public:
        ~Environment() noexcept = default;
        Environment() = delete;
        explicit Environment(Handle handle) noexcept : m_handle(handle) {}
        Environment(const Environment &) = default;
        Environment(Environment &&) = default;
        Environment & operator=(const Environment &) = default;
        Environment & operator=(Environment &&) = default;
    public:
        Handle query(exec::get_completion_scheduler_t<exec::set_value_t>) const noexcept { return m_handle; }
    private:
        Handle m_handle;
    };
public:
    using sender_concept = protocols::sender_t;
    using completion_signatures = ScheduleCompletions;
public:
    ~ScheduleSender() noexcept = default;
    ScheduleSender() = delete;
    explicit ScheduleSender(Handle handle) noexcept : m_handle(handle) {}
    ScheduleSender(const ScheduleSender &) = default;
    ScheduleSender(ScheduleSender &&) = default;
    ScheduleSender & operator=(const ScheduleSender &) = default;
    ScheduleSender & operator=(ScheduleSender &&) = default;
public:
    Environment get_env() const noexcept { return Environment {m_handle}; }
    template <receiver_of_c<ScheduleCompletions> Receiver>
    ScheduleOperation<Handle, Receiver> connect(Receiver receiver) const
    {
        return ScheduleOperation<Handle, Receiver> {m_handle, std::move(receiver)};
    }
private:
    Handle m_handle;
};

} // namespace lcf::async::details
