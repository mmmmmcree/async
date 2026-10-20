#pragma once

#include <async/core/protocol/details/completion.h>
#include <expected>
#include <mutex>
#include <stdexcept>

namespace lcf::async::exec {

LCF_ASYNC_EXPORT
class RetryContext
{
public:
    explicit RetryContext(std::size_t attempt) noexcept : m_attempt(attempt) {}
public:
    std::size_t getAttemptCount() const noexcept { return m_attempt; }
private:
    std::size_t m_attempt;
};

namespace details {

template <typename Value, typename Error>
struct ResultSender
{
    using sender_concept = sender_t;
    using completion_signatures = concatenate_t<value_signature_t<return_tuple_t<Value>>, exec::completion_signatures<set_error_t(Error)>>;
    template <typename Receiver>
    struct Operation
    {
        using operation_state_concept = operation_state_t;
        void start() noexcept
        {
            if (not m_result) { set_error(std::move(m_receiver), std::move(m_result).error()); return; }
            if constexpr (std::is_void_v<Value>) { set_value(std::move(m_receiver)); }
            else { set_value(std::move(m_receiver), std::move(m_result).value()); }
        }
        Receiver m_receiver;
        std::expected<Value, Error> m_result;
    };
    template <receiver Receiver>
    auto connect(Receiver receiver) && { return Operation<Receiver> {std::move(receiver), std::move(m_result)}; }
    std::expected<Value, Error> m_result;
};
struct RetryErrors
{
    template <typename Error>
    bool operator()(const Error &, const RetryContext &) const noexcept { return true; }
};

template <typename Factory, typename Predicate, typename Receiver>
class RetryOperation
{
    using Self = RetryOperation;
    using Source = std::invoke_result_t<Factory &, const RetryContext &>;
    using SourceReceiver = ChildReceiver<Self, env_of_t<Receiver>>;
    using ChildOperation = connect_result_t<Source, SourceReceiver>;
    using Signatures = concatenate_t<completion_signatures_of_t<Source>, completion_signatures<set_error_t(std::exception_ptr), set_stopped_t()>>;
public:
    RetryOperation(Factory factory, Predicate predicate, std::size_t max_attempts, Receiver receiver) :
        m_receiver(std::move(receiver)), m_factory(std::move(factory)), m_predicate(std::move(predicate)), m_max_attempts(max_attempts) {}
    RetryOperation(const Self &) = delete;
    Self & operator=(const Self &) = delete;
public:
    using operation_state_concept = operation_state_t;
    void start() noexcept { this->drive(); }
    template <std::size_t, typename Tag, typename... Values>
    void complete(Tag tag, Values &&... values) noexcept
    {
        if constexpr (std::same_as<Tag, set_error_t> and std::invocable<Predicate &, Values &..., const RetryContext &>) {
            try {
                if (m_attempt < m_max_attempts and std::invoke(m_predicate, values..., RetryContext {m_attempt})) {
                    { std::lock_guard lock {m_mutex}; m_again = true; }
                    this->drive();
                    return;
                }
            } catch (...) { this->publish(set_error, std::current_exception()); return; }
        }
        this->publish(tag, std::forward<Values>(values)...);
    }
private:
    template <typename Tag, typename... Values>
    void publish(Tag tag, Values &&... values) noexcept
    {
        {
            std::lock_guard lock {m_mutex};
            m_terminal.publish(tag, std::forward<Values>(values)...);
            m_terminal_ready = true;
        }
        this->drive();
    }
    void drive() noexcept
    {
        {
            std::lock_guard lock {m_mutex};
            if (m_driving) { return; }
            m_driving = true;
        }
        for (;;) {
            bool terminal;
            {
                std::lock_guard lock {m_mutex};
                terminal = m_terminal_ready;
                if (not terminal and not m_again) { m_driving = false; return; }
                m_again = false;
            }
            if (terminal) { m_terminal.deliver(std::move(m_receiver)); return; }
            if (get_stop_token(get_env(m_receiver)).stop_requested()) { set_stopped(std::move(m_receiver)); return; }
            try {
                const RetryContext context {++m_attempt};
                auto child = std::invoke(m_factory, context);
                m_child.emplace([&] { return exec::connect(std::move(child), SourceReceiver {this, get_env(m_receiver)}); });
                exec::start(m_child.get());
            } catch (...) { this->template complete<0>(set_error, std::current_exception()); }
        }
    }
private:
    Receiver m_receiver;
    Factory m_factory;
    Predicate m_predicate;
    std::size_t m_max_attempts;
    std::size_t m_attempt = 0;
    CompletionStorage<Signatures> m_terminal;
    std::mutex m_mutex;
    bool m_driving = false;
    bool m_again = true;
    bool m_terminal_ready = false;
    OperationStorage<ChildOperation> m_child;
};
template <typename Factory, typename Predicate>
struct RetrySender
{
    using sender_concept = sender_t;
    using Source = std::invoke_result_t<Factory &, const RetryContext &>;
    using completion_signatures = concatenate_t<completion_signatures_of_t<Source>, exec::completion_signatures<set_error_t(std::exception_ptr), set_stopped_t()>>;
    template <receiver Receiver>
    auto connect(Receiver receiver) &&
    { return RetryOperation<Factory, Predicate, Receiver> {std::move(m_factory), std::move(m_predicate), m_max_attempts, std::move(receiver)}; }
    Factory m_factory;
    Predicate m_predicate;
    std::size_t m_max_attempts;
};

}

LCF_ASYNC_EXPORT
template <typename Value, typename Error>
auto from_result(std::expected<Value, Error> result) { return details::ResultSender<Value, Error> {std::move(result)}; }
LCF_ASYNC_EXPORT
template <typename Factory, typename Predicate = details::RetryErrors>
    requires std::invocable<Factory &, const RetryContext &> and sender<std::invoke_result_t<Factory &, const RetryContext &>>
auto retry(Factory factory, std::size_t max_attempts, Predicate predicate = {})
{
    if (max_attempts == 0) { throw std::invalid_argument("retry requires at least one attempt"); }
    return details::RetrySender<Factory, Predicate> {std::move(factory), std::move(predicate), max_attempts};
}

}
