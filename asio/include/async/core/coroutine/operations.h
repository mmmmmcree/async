#pragma once

#include <async/core/coroutine/Awaitable.h>
#include <async/core/protocol/details/utility.h>
#include <async/core/scheduling/Scheduler.h>
#include <asio/bind_cancellation_slot.hpp>
#include <asio/cancellation_signal.hpp>
#include <asio/co_spawn.hpp>
#include <asio/strand.hpp>

namespace lcf::async::details {

class CoroutineCancellation
{
public:
    explicit CoroutineCancellation(Executor executor) : m_strand(asio::make_strand(std::move(executor))) {}
public:
    Executor getExecutor() const noexcept { return m_strand; }
    auto getSlot() noexcept { return m_signal.slot(); }
    void cancel() { if (not m_finished) { m_signal.emit(asio::cancellation_type::terminal); } }
    void finish() noexcept { m_finished = true; }
private:
    asio::strand<Executor> m_strand;
    asio::cancellation_signal m_signal;
    bool m_finished = false;
};

template <typename Value, typename Receiver>
class CoroutineOperation
{
    using Self = CoroutineOperation;
    using Values = exec::details::return_tuple_t<Value>;
    struct Cancel
    {
        void operator()() const noexcept
        {
            try { lcf::async::post(m_cancellation_sp->getExecutor(), [bridge_sp = m_cancellation_sp] { bridge_sp->cancel(); }); }
            catch (...) {
                // Stop is cooperative: failed submission leaves the token visible to launch/completion.
            }
        }
        std::shared_ptr<CoroutineCancellation> m_cancellation_sp;
    };
    using StopCallback = std::stop_callback<Cancel>;
public:
    CoroutineOperation(Receiver receiver, Scheduler scheduler, Awaitable<Value> work) :
        m_receiver(std::move(receiver)), m_work(std::move(work)),
        m_cancellation_sp(std::allocate_shared<CoroutineCancellation>(exec::get_allocator(exec::get_env(m_receiver)), scheduler.getExecutor())) {}
    CoroutineOperation(const Self &) = delete;
    Self & operator=(const Self &) = delete;
public:
    using operation_state_concept = exec::operation_state_t;
    void start() noexcept
    {
        try {
            m_stop_callback_opt.emplace(exec::get_stop_token(exec::get_env(m_receiver)), Cancel {m_cancellation_sp});
            lcf::async::post(m_cancellation_sp->getExecutor(), [this]() noexcept { this->launch(); });
        } catch (...) {
            m_stop_callback_opt.reset();
            exec::set_error(std::move(m_receiver), std::current_exception());
        }
    }
private:
    void launch() noexcept
    {
        if (exec::get_stop_token(exec::get_env(m_receiver)).stop_requested()) {
            m_cancellation_sp->finish();
            m_stop_callback_opt.reset();
            exec::set_stopped(std::move(m_receiver));
            return;
        }
        try {
            asio::co_spawn(m_cancellation_sp->getExecutor(), Self::execute(std::move(m_work), m_values_opt),
                asio::bind_cancellation_slot(m_cancellation_sp->getSlot(), [this](std::exception_ptr error) noexcept { this->complete(std::move(error)); }));
        } catch (...) { this->complete(std::current_exception()); }
    }
    void complete(std::exception_ptr error) noexcept
    {
        m_cancellation_sp->finish();
        const bool stop_requested = exec::get_stop_token(exec::get_env(m_receiver)).stop_requested();
        m_stop_callback_opt.reset();
        if (error) {
            try { std::rethrow_exception(error); }
            catch (const std::system_error & exception) {
                if (stop_requested and exception.code() == asio::error::operation_aborted) { exec::set_stopped(std::move(m_receiver)); return; }
            } catch (...) {}
            exec::set_error(std::move(m_receiver), std::move(error));
            return;
        }
        std::apply([this](auto &... values) noexcept { exec::set_value(std::move(m_receiver), std::move(values)...); }, *m_values_opt);
    }
    static Awaitable<void> execute(Awaitable<Value> work, std::optional<Values> & values_opt)
    {
        if constexpr (std::is_void_v<Value>) { co_await std::move(work); values_opt.emplace(); }
        else { values_opt.emplace(co_await std::move(work)); }
    }
private:
    Receiver m_receiver;
    Awaitable<Value> m_work;
    std::optional<Values> m_values_opt;
    std::shared_ptr<CoroutineCancellation> m_cancellation_sp;
    std::optional<StopCallback> m_stop_callback_opt;
};

template <typename Value>
struct CoroutineSender
{
    using sender_concept = exec::sender_t;
    using completion_signatures = exec::details::concatenate_t<exec::details::value_signature_t<exec::details::return_tuple_t<Value>>,
        exec::completion_signatures<exec::set_error_t(std::exception_ptr), exec::set_stopped_t()>>;
    template <exec::receiver Receiver>
    auto connect(Receiver receiver) && { return CoroutineOperation<Value, Receiver> {std::move(receiver), std::move(m_scheduler), std::move(m_work)}; }
    Scheduler m_scheduler;
    Awaitable<Value> m_work;
};

}

namespace lcf::async {

LCF_ASYNC_EXPORT
template <typename Value>
auto as_sender(Scheduler scheduler, Awaitable<Value> work) { return details::CoroutineSender<Value> {std::move(scheduler), std::move(work)}; }

}
