#pragma once

#include <async/core/protocol.h>
#include <functional>
#include <memory>
#include <async/core/scheduling/details/callback.h>

namespace lcf::async::details {

template <typename Receiver, typename Target>
class ScheduledOperation
{
    using Self = ScheduledOperation;
public:
    using operation_state_concept = exec::operation_state_t;
    ScheduledOperation(Target target, Receiver receiver) :
        m_target(std::move(target)), m_receiver(std::move(receiver)), m_callback(Callback::make<&Self::invoke>(this)) {}
    ScheduledOperation(const Self &) = delete;
    Self & operator=(const Self &) = delete;
public:
    void start() noexcept { m_target.enqueue(&m_callback); }
private:
    static void invoke(Callback * callback_p) noexcept
    {
        auto & operation = *static_cast<Self *>(callback_p->m_owner_p);
        if (exec::get_stop_token(exec::get_env(operation.m_receiver)).stop_requested()) { exec::set_stopped(std::move(operation.m_receiver)); }
        else { exec::set_value(std::move(operation.m_receiver)); }
    }
private:
    Target m_target;
    Receiver m_receiver;
    Callback m_callback;
};

template <typename Target, typename Function>
void post_callback(const Target & target, Function function)
{
    struct Record
    {
        explicit Record(Function function) : m_callback(Callback::make<&Record::invoke, &Record::abandon>(this)), m_function(std::move(function)) {}
        static void invoke(Callback * callback_p)
        {
            std::unique_ptr<Record> record_up {static_cast<Record *>(callback_p->m_owner_p)};
            std::invoke(record_up->m_function);
        }
        static void abandon(Callback * callback_p) noexcept { delete static_cast<Record *>(callback_p->m_owner_p); }
        Callback m_callback;
        Function m_function;
    };
    auto record_up = std::make_unique<Record>(std::move(function));
    auto * record_p = record_up.release();
    target.enqueue(&record_p->m_callback);
}

}
