#pragma once

#include <async/core/protocol/details/completion.h>
#include <atomic>
#include <mutex>

namespace lcf::async::exec::details {

template <typename Signatures>
struct without_values;
template <>
struct without_values<completion_signatures<>> { using type = completion_signatures<>; };
template <typename... Values, typename... Rest>
struct without_values<completion_signatures<set_value_t(Values...), Rest...>> : without_values<completion_signatures<Rest...>> {};
template <typename First, typename... Rest>
struct without_values<completion_signatures<First, Rest...>>
{ using type = concatenate_t<completion_signatures<First>, typename without_values<completion_signatures<Rest...>>::type>; };
template <typename... Sources>
struct when_all_signatures
{
    using tuple = decltype(std::tuple_cat(std::declval<value_tuple_t<Sources>>()...));
    using values = std::conditional_t<(value_tuple<completion_signatures_of_t<Sources>>::present and ...), value_signature_t<tuple>, completion_signatures<>>;
    using type = concatenate_t<values, typename without_values<completion_signatures_of_t<Sources>>::type...,
        completion_signatures<set_error_t(std::exception_ptr), set_stopped_t()>>;
};

template <typename Receiver, typename... Sources>
class WhenAllOperation
{
    using Self = WhenAllOperation;
    using Environment = EnvironmentOverride<env_of_t<Receiver>, get_stop_token_t, std::stop_token>;
    template <std::size_t Index>
    using Child = ChildReceiver<Self, Environment, Index>;
    template <std::size_t... Indices>
    static auto operation_types(std::index_sequence<Indices...>) -> std::tuple<OperationStorage<connect_result_t<Sources, Child<Indices>>>...>;
    using Operations = decltype(operation_types(std::index_sequence_for<Sources...> {}));
    using Values = std::tuple<std::optional<value_tuple_t<Sources>>...>;
    struct Cancel
    {
        void operator()() const noexcept { m_source.request_stop(); }
        mutable std::stop_source m_source;
    };
public:
    WhenAllOperation(std::tuple<Sources...> sources, Receiver receiver) : m_receiver(std::move(receiver))
    {
        [&]<std::size_t... Indices>(std::index_sequence<Indices...>) {
            (std::get<Indices>(m_operations).emplace([&] {
                return exec::connect(std::move(std::get<Indices>(sources)), Child<Indices> {this, {get_env(m_receiver), m_stop_source.get_token()}});
            }), ...);
        }(std::index_sequence_for<Sources...> {});
    }
    WhenAllOperation(const Self &) = delete;
    Self & operator=(const Self &) = delete;
public:
    using operation_state_concept = operation_state_t;
    void start() noexcept
    {
        m_stop_callback_opt.emplace(get_stop_token(get_env(m_receiver)), Cancel {m_stop_source});
        std::apply([](auto &... operations) noexcept { (exec::start(operations.get()), ...); }, m_operations);
        this->arrive();
    }
    template <std::size_t Index, typename Tag, typename... Arguments>
    void complete(Tag tag, Arguments &&... arguments) noexcept
    {
        bool cancel = false;
        {
            std::lock_guard lock {m_mutex};
            if constexpr (std::same_as<Tag, set_value_t>) {
                try { std::get<Index>(m_values).emplace(std::forward<Arguments>(arguments)...); }
                catch (...) {
                    if (not m_error) { m_terminal.publish(set_error, std::current_exception()); }
                    m_error = true;
                    cancel = true;
                }
            } else if constexpr (std::same_as<Tag, set_error_t>) {
                if (not m_error) { m_terminal.publish(tag, std::forward<Arguments>(arguments)...); }
                m_error = true;
                cancel = true;
            } else { m_stopped = true; cancel = true; }
        }
        if (cancel) { m_stop_source.request_stop(); }
        this->arrive();
    }
private:
    void arrive() noexcept
    {
        if (m_remaining.fetch_sub(1, std::memory_order_acq_rel) != 1) { return; }
        m_stop_callback_opt.reset();
        if (m_error) { m_terminal.deliver(std::move(m_receiver)); return; }
        if (m_stopped) { set_stopped(std::move(m_receiver)); return; }
        if constexpr ((value_tuple<completion_signatures_of_t<Sources>>::present and ...)) {
            try {
                auto values = std::apply([](auto &... values_opt) { return std::tuple_cat(std::move(*values_opt)...); }, m_values);
                std::apply([this](auto &... results) noexcept { set_value(std::move(m_receiver), std::move(results)...); }, values);
            } catch (...) { set_error(std::move(m_receiver), std::current_exception()); }
        }
    }
private:
    Receiver m_receiver;
    Values m_values;
    CompletionStorage<typename when_all_signatures<Sources...>::type> m_terminal;
    std::mutex m_mutex;
    std::atomic_size_t m_remaining = sizeof...(Sources) + 1;
    std::stop_source m_stop_source;
    std::optional<std::stop_callback<Cancel>> m_stop_callback_opt;
    bool m_error = false;
    bool m_stopped = false;
    Operations m_operations;
};
template <typename... Sources>
struct WhenAllSender
{
    using sender_concept = sender_t;
    using completion_signatures = typename when_all_signatures<Sources...>::type;
    template <receiver Receiver>
    auto connect(Receiver receiver) && { return WhenAllOperation<Receiver, Sources...> {std::move(m_sources), std::move(receiver)}; }
    std::tuple<Sources...> m_sources;
};

}

namespace lcf::async::exec {

LCF_ASYNC_EXPORT
template <sender... Sources>
    requires (sizeof...(Sources) > 0)
auto when_all(Sources &&... sources) { return details::WhenAllSender<std::decay_t<Sources>...> {{std::forward<Sources>(sources)...}}; }

}
