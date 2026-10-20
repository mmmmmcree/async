#pragma once

#include <async/core/scheduling/details/scheduled_operation.h>
#include <cassert>
#include <functional>
#include <memory>

namespace lcf::async {

LCF_ASYNC_EXPORT
class Scheduler
{
    friend class Context;
    using Self = Scheduler;
    using QueueHandle = std::shared_ptr<details::Queue>;
public:
    struct ScheduleSender
    {
        using sender_concept = exec::sender_t;
        using completion_signatures = exec::completion_signatures<exec::set_value_t(), exec::set_stopped_t()>;
        struct Environment
        {
            QueueHandle m_queue_sp;
            Scheduler query(exec::get_completion_scheduler_t<exec::set_value_t>) const noexcept { return Scheduler {m_queue_sp}; }
        };
        Environment get_env() const noexcept { return {m_queue_sp}; }
        template <exec::receiver Receiver>
        auto connect(Receiver receiver) const { return details::ScheduledOperation<Receiver, Scheduler> {Scheduler {m_queue_sp}, std::move(receiver)}; }
        QueueHandle m_queue_sp;
    };
public:
    Scheduler() noexcept = default;
    bool operator==(const Self &) const noexcept = default;
public:
    ScheduleSender schedule() const noexcept
    {
        assert(m_queue_sp and "a default-constructed Scheduler has no queue; use Context::getScheduler()");
        return {m_queue_sp};
    }
    void enqueue(details::Callback * callback_p) const noexcept { m_queue_sp->push(callback_p); }
    void workStarted() const noexcept { m_queue_sp->workStarted(); }
    void workFinished() const noexcept { m_queue_sp->workFinished(); }
    template <typename Function>
    void post(Function function) const { details::post_callback(*this, std::move(function)); }
private:
    explicit Scheduler(QueueHandle queue_sp) noexcept : m_queue_sp(std::move(queue_sp)) {}
private:
    QueueHandle m_queue_sp;
};

}
