#pragma once

#include <async/core/protocol.h>
#include <async/core/scheduling/operations.h>

namespace lcf::async {

LCF_ASYNC_EXPORT
class Scheduler
{
    using Self = Scheduler;
    struct Environment;
    struct ScheduleSender;
public:
    explicit Scheduler(Executor executor) noexcept : m_executor(std::move(executor)) {}
public:
    auto schedule() const noexcept;
    const Executor & getExecutor() const noexcept { return m_executor; }
    friend bool operator==(const Self &, const Self &) noexcept = default;
private:
    Executor m_executor;
};

struct Scheduler::Environment
{
    Scheduler query(exec::get_completion_scheduler_t<exec::set_value_t>) const noexcept { return m_scheduler; }
    Scheduler m_scheduler;
};

struct Scheduler::ScheduleSender
{
    using sender_concept = exec::sender_t;
    using completion_signatures = exec::completion_signatures<exec::set_value_t(), exec::set_error_t(std::exception_ptr), exec::set_stopped_t()>;
    template <typename Receiver>
    class Operation
    {
    public:
        Operation(Receiver receiver, Executor executor) : m_receiver(std::move(receiver)), m_executor(std::move(executor)) {}
        Operation(const Operation &) = delete;
        Operation & operator=(const Operation &) = delete;
    public:
        using operation_state_concept = exec::operation_state_t;
        void start() noexcept
        {
            if (exec::get_stop_token(exec::get_env(m_receiver)).stop_requested()) {
                exec::set_stopped(std::move(m_receiver));
                return;
            }
            try {
                lcf::async::post(m_executor, [this]() noexcept {
                    if (exec::get_stop_token(exec::get_env(m_receiver)).stop_requested()) { exec::set_stopped(std::move(m_receiver)); }
                    else { exec::set_value(std::move(m_receiver)); }
                });
            } catch (...) { exec::set_error(std::move(m_receiver), std::current_exception()); }
        }
    private:
        Receiver m_receiver;
        Executor m_executor;
    };
    auto get_env() const noexcept { return Environment {m_scheduler}; }
    template <exec::receiver Receiver>
    auto connect(Receiver receiver) const { return Operation<Receiver> {std::move(receiver), m_scheduler.getExecutor()}; }
    Scheduler m_scheduler;
};

inline auto Scheduler::schedule() const noexcept { return ScheduleSender {*this}; }

}
