#pragma once

#include <async/execution/details/adaptor.h>
#include <async/execution/factories.h>
#include <expected>
#include <system_error>

namespace lcf::async::exec::ext::details {

template <typename Result>
struct ExpectedResult { static constexpr bool valid = false; };

template <typename Value>
struct ExpectedResult<std::expected<Value, std::error_code>>
{
    static constexpr bool valid = true;
    using completion_signatures = exec::details::concatenate_t<exec::details::value_signature_t<exec::details::return_tuple_t<Value>>,
        exec::completion_signatures<set_error_t(std::error_code)>>;
};

template <typename Function, typename... Values>
concept expected_function = std::is_nothrow_invocable_v<Function, Values...>
    and ExpectedResult<std::invoke_result_t<Function, Values...>>::valid;

template <typename Function, typename Signature>
struct ValidExpectedSignature : std::true_type {};

template <typename Function, typename... Values>
struct ValidExpectedSignature<Function, set_value_t(Values...)> : std::bool_constant<expected_function<Function, Values...>> {};

template <typename Function, typename Signatures>
struct ValidExpectedSignatures;

template <typename Function, typename... Signatures>
struct ValidExpectedSignatures<Function, completion_signatures<Signatures...>> :
    std::bool_constant<(ValidExpectedSignature<Function, Signatures>::value and ...)> {};

template <typename Source, typename Function>
concept expected_continuation = sender<Source> and std::move_constructible<Function>
    and ValidExpectedSignatures<Function, completion_signatures_of_t<Source>>::value;

template <typename Function, typename Signature>
struct ExpectedSignature { using type = completion_signatures<Signature>; };

template <typename Function, typename... Values>
struct ExpectedSignature<Function, set_value_t(Values...)>
{ using type = typename ExpectedResult<std::invoke_result_t<Function, Values...>>::completion_signatures; };

template <typename Function, typename Signatures>
struct ExpectedSignatures;

template <typename Function, typename... Signatures>
struct ExpectedSignatures<Function, completion_signatures<Signatures...>>
{ using type = exec::details::concatenate_t<typename ExpectedSignature<Function, Signatures>::type...>; };

template <typename Source, typename Function, typename Receiver>
class ExpectedOperation
{
    using Self = ExpectedOperation;
    using SourceReceiver = exec::details::ChildReceiver<Self, env_of_t<Receiver>>;
    using SourceOperation = connect_result_t<Source, SourceReceiver>;
public:
    ExpectedOperation(Source source, Function function, Receiver receiver) : m_receiver(std::move(receiver)), m_function(std::move(function))
    { m_source.emplace([&] { return exec::connect(std::move(source), SourceReceiver {this, get_env(m_receiver)}); }); }
    ExpectedOperation(const Self &) = delete;
    Self & operator=(const Self &) = delete;
public:
    using operation_state_concept = operation_state_t;
    void start() noexcept { exec::start(m_source.get()); }
    template <std::size_t, typename Tag, typename... Values>
    void complete(Tag tag, Values &&... values) noexcept
    {
        if constexpr (not std::same_as<Tag, set_value_t>) { tag(std::move(m_receiver), std::forward<Values>(values)...); }
        else {
            auto result = std::invoke(std::move(m_function), std::forward<Values>(values)...);
            if (not result) { set_error(std::move(m_receiver), std::move(result).error()); }
            else if constexpr (std::is_void_v<typename decltype(result)::value_type>) { set_value(std::move(m_receiver)); }
            else { set_value(std::move(m_receiver), *std::move(result)); }
        }
    }
private:
    Receiver m_receiver;
    Function m_function;
    exec::details::OperationStorage<SourceOperation> m_source;
};

template <typename Source, typename Function>
struct ExpectedSender
{
    using sender_concept = sender_t;
    using completion_signatures = typename ExpectedSignatures<Function, completion_signatures_of_t<Source>>::type;
    auto get_env() const noexcept { return exec::get_env(m_source); }
    template <receiver Receiver>
    auto connect(Receiver receiver) &&
    { return ExpectedOperation<Source, Function, Receiver> {std::move(m_source), std::move(m_function), std::move(receiver)}; }
    Source m_source;
    Function m_function;
};

struct ThenExpected
{
    template <sender Source, typename Function>
        requires expected_continuation<Source, Function>
    auto operator()(Source && source, Function function) const
    { return ExpectedSender<std::decay_t<Source>, Function> {std::forward<Source>(source), std::move(function)}; }
    template <typename Function>
        requires std::move_constructible<Function>
    auto operator()(Function function) const
    {
        return exec::details::Adaptor {[function = std::move(function)]<sender Source>(Source && source) mutable
            requires expected_continuation<Source, Function> {
            return ThenExpected {}(std::forward<Source>(source), std::move(function));
        }};
    }
};

}

namespace lcf::async::exec::ext {

// Like then, but routes a noexcept callback's expected result to value/error channels.
LCF_ASYNC_EXPORT
inline constexpr details::ThenExpected then_expected {};

// Stores decayed arguments; invokes the callable once, on the thread that starts it.
LCF_ASYNC_EXPORT
template <typename Function, typename... Arguments>
    requires std::move_constructible<Function> and (std::constructible_from<std::decay_t<Arguments>, Arguments> and ...)
        and details::expected_function<Function, std::decay_t<Arguments>...>
auto from_expected(Function function, Arguments &&... arguments)
{ return then_expected(exec::just(std::forward<Arguments>(arguments)...), std::move(function)); }

}
