#pragma once

#include <async/core/protocol/details/utility.h>
#include <async/execution/details/adaptor.h>

namespace lcf::async::exec::details {

template <typename Match, typename Function, typename Signature, bool Expand>
struct valid_continuation_signature : std::true_type {};
template <typename Match, typename Function, typename... Values>
struct valid_continuation_signature<Match, Function, Match(Values...), false> : std::bool_constant<std::invocable<Function, Values...>> {};
template <typename Match, typename Function, typename... Values>
struct valid_continuation_signature<Match, Function, Match(Values...), true>
{
    static constexpr bool value = [] {
        if constexpr (std::invocable<Function, std::remove_reference_t<Values> &...>) {
            return sender<std::invoke_result_t<Function, std::remove_reference_t<Values> &...>>;
        } else { return false; }
    }();
};
template <typename Match, typename Function, typename Signatures, bool Expand>
struct valid_continuation;
template <typename Match, typename Function, typename... Signatures, bool Expand>
struct valid_continuation<Match, Function, completion_signatures<Signatures...>, Expand> :
    std::bool_constant<(valid_continuation_signature<Match, Function, Signatures, Expand>::value and ...)> {};
template <typename Source, typename Function, typename Match, bool Expand>
concept continuation_c = sender<Source> and std::move_constructible<Function>
    and valid_continuation<Match, Function, completion_signatures_of_t<Source>, Expand>::value;

template <typename Match, typename Function, typename Signature, bool Expand>
struct continuation_signature { using type = completion_signatures<Signature>; };
template <typename Match, typename Function, typename... Values>
struct continuation_signature<Match, Function, Match(Values...), false>
{ using type = value_signature_t<return_tuple_t<std::invoke_result_t<Function, Values...>>>; };
template <typename Match, typename Function, typename... Values>
struct continuation_signature<Match, Function, Match(Values...), true>
{
    using result = std::invoke_result_t<Function, std::remove_reference_t<Values> &...>;
    static_assert(sender<result>, "let_* callbacks must return a sender.");
    using type = completion_signatures_of_t<result>;
};
template <typename Match, typename Function, typename Signatures, bool Expand>
struct continuation_signatures;
template <typename Match, typename Function, typename... Signatures, bool Expand>
struct continuation_signatures<Match, Function, completion_signatures<Signatures...>, Expand>
{ using type = concatenate_t<typename continuation_signature<Match, Function, Signatures, Expand>::type..., completion_signatures<set_error_t(std::exception_ptr)>>; };

template <typename Function, typename Receiver, typename Match, bool Expand>
class ContinuationState
{
    using Self = ContinuationState;
    using Environment = env_of_t<Receiver>;
public:
    ContinuationState(Function function, Receiver receiver) : m_receiver(std::move(receiver)), m_function(std::move(function)) {}
public:
    auto getEnvironment() const noexcept { return get_env(m_receiver); }
    template <std::size_t Index, typename Tag, typename... Values>
    void complete(Tag tag, Values &&... values) noexcept
    {
        if constexpr (Index == 1 or not std::same_as<Tag, Match>) { tag(std::move(m_receiver), std::forward<Values>(values)...); }
        else {
            try {
                if constexpr (Expand) {
                    using Inputs = std::tuple<std::conditional_t<std::is_lvalue_reference_v<Values>, Values, std::decay_t<Values>>...>;
                    auto inputs_sp = std::allocate_shared<Inputs>(get_allocator(get_env(m_receiver)), std::forward<Values>(values)...);
                    auto inner = std::apply(std::move(m_function), *inputs_sp);
                    using InnerReceiver = ChildReceiver<Self, Environment, 1>;
                    using InnerOperation = connect_result_t<decltype(inner), InnerReceiver>;
                    m_inputs_sp = std::move(inputs_sp);
                    auto operation_sp = std::allocate_shared<OperationStorage<InnerOperation>>(get_allocator(get_env(m_receiver)));
                    operation_sp->emplace([&] { return exec::connect(std::move(inner), InnerReceiver {this, get_env(m_receiver)}); });
                    auto * operation_p = &operation_sp->get();
                    m_inner_sp = std::move(operation_sp);
                    exec::start(*operation_p);
                } else if constexpr (std::is_void_v<std::invoke_result_t<Function, Values...>>) {
                    std::invoke(std::move(m_function), std::forward<Values>(values)...);
                    set_value(std::move(m_receiver));
                } else {
                    auto result = std::invoke(std::move(m_function), std::forward<Values>(values)...);
                    set_value(std::move(m_receiver), std::move(result));
                }
            } catch (...) { set_error(std::move(m_receiver), std::current_exception()); }
        }
    }
private:
    Receiver m_receiver;
    Function m_function;
    std::shared_ptr<void> m_inputs_sp;
    std::shared_ptr<void> m_inner_sp;
};

template <typename Source, typename Function, typename Receiver, typename Match, bool Expand>
class ContinuationOperation
{
    using Self = ContinuationOperation;
    using State = ContinuationState<Function, Receiver, Match, Expand>;
    using SourceReceiver = ChildReceiver<State, env_of_t<Receiver>>;
    using SourceOperation = connect_result_t<Source, SourceReceiver>;
public:
    ContinuationOperation(Source source, Function function, Receiver receiver) : m_state(std::move(function), std::move(receiver))
    {
        m_source.emplace([&] { return exec::connect(std::move(source), SourceReceiver {&m_state, m_state.getEnvironment()}); });
    }
    ContinuationOperation(const Self &) = delete;
    Self & operator=(const Self &) = delete;
public:
    using operation_state_concept = operation_state_t;
    void start() noexcept { exec::start(m_source.get()); }
private:
    State m_state;
    OperationStorage<SourceOperation> m_source;
};

template <typename Source, typename Function, typename Match, bool Expand>
struct ContinuationSender
{
    using sender_concept = sender_t;
    using completion_signatures = typename continuation_signatures<Match, Function, completion_signatures_of_t<Source>, Expand>::type;
    auto get_env() const noexcept { return exec::get_env(m_source); }
    template <receiver Receiver>
    auto connect(Receiver receiver) &&
    { return ContinuationOperation<Source, Function, Receiver, Match, Expand> {std::move(m_source), std::move(m_function), std::move(receiver)}; }
    Source m_source;
    Function m_function;
};
template <typename Match, bool Expand>
struct ContinuationAlgorithm
{
    template <sender Source, typename Function>
        requires continuation_c<Source, Function, Match, Expand>
    auto operator()(Source && source, Function function) const
    { return ContinuationSender<std::decay_t<Source>, Function, Match, Expand> {std::forward<Source>(source), std::move(function)}; }
    template <typename Function>
    auto operator()(Function function) const
    {
        return Adaptor {[function = std::move(function)]<sender Source>(Source && source) mutable
            requires continuation_c<Source, Function, Match, Expand> {
            return ContinuationAlgorithm {}(std::forward<Source>(source), std::move(function));
        }};
    }
};

}

namespace lcf::async::exec {

LCF_ASYNC_EXPORT
inline constexpr details::ContinuationAlgorithm<set_value_t, false> then {};
LCF_ASYNC_EXPORT
inline constexpr details::ContinuationAlgorithm<set_error_t, false> upon_error {};
LCF_ASYNC_EXPORT
inline constexpr details::ContinuationAlgorithm<set_stopped_t, false> upon_stopped {};
LCF_ASYNC_EXPORT
inline constexpr details::ContinuationAlgorithm<set_value_t, true> let_value {};
LCF_ASYNC_EXPORT
inline constexpr details::ContinuationAlgorithm<set_error_t, true> let_error {};
LCF_ASYNC_EXPORT
inline constexpr details::ContinuationAlgorithm<set_stopped_t, true> let_stopped {};

}
