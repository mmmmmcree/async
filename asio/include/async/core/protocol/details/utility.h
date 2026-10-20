#pragma once

#include <async/core/protocol/operations.h>
#include <functional>
#include <memory>
#include <optional>
#include <tuple>
#include <variant>

namespace lcf::async::exec::details {

template <typename... Types>
struct type_list {};
template <typename List, typename Type>
struct append_unique;
template <typename... Types, typename Type>
struct append_unique<type_list<Types...>, Type>
{ using type = std::conditional_t<(std::same_as<Type, Types> or ...), type_list<Types...>, type_list<Types..., Type>>; };
template <typename List, typename... Types>
struct unique_list;
template <typename List>
struct unique_list<List> { using type = List; };
template <typename List, typename Type, typename... Rest>
struct unique_list<List, Type, Rest...> : unique_list<typename append_unique<List, Type>::type, Rest...> {};
template <typename List>
struct as_signatures;
template <typename... Signatures>
struct as_signatures<type_list<Signatures...>> { using type = completion_signatures<Signatures...>; };
template <typename... Signatures>
struct concatenate;
template <>
struct concatenate<> { using type = completion_signatures<>; };
template <typename... Left, typename... Right, typename... Rest>
struct concatenate<completion_signatures<Left...>, completion_signatures<Right...>, Rest...> :
    concatenate<completion_signatures<Left..., Right...>, Rest...> {};
template <typename... Signatures>
struct concatenate<completion_signatures<Signatures...>>
{ using type = typename as_signatures<typename unique_list<type_list<>, Signatures...>::type>::type; };
template <typename... Signatures>
using concatenate_t = typename concatenate<Signatures...>::type;

template <typename Signature>
struct signature_traits;
template <typename Tag, typename... Arguments>
struct signature_traits<Tag(Arguments...)>
{
    using tag = Tag;
    using tuple = std::tuple<std::decay_t<Arguments>...>;
};
template <typename Signatures>
struct value_tuple;
template <>
struct value_tuple<completion_signatures<>> { using type = std::tuple<>; static constexpr bool present = false; };
template <typename... Values, typename... Rest>
struct value_tuple<completion_signatures<set_value_t(Values...), Rest...>>
{
    static_assert(not value_tuple<completion_signatures<Rest...>>::present, "Only one distinct value signature per sender is supported.");
    using type = std::tuple<std::decay_t<Values>...>;
    static constexpr bool present = true;
};
template <typename First, typename... Rest>
struct value_tuple<completion_signatures<First, Rest...>> : value_tuple<completion_signatures<Rest...>> {};
template <typename Sender>
using value_tuple_t = typename value_tuple<completion_signatures_of_t<Sender>>::type;
template <typename Tuple>
struct value_signature;
template <typename... Values>
struct value_signature<std::tuple<Values...>> { using type = completion_signatures<set_value_t(Values...)>; };
template <typename Tuple>
using value_signature_t = typename value_signature<Tuple>::type;
template <typename Value>
struct return_tuple { using type = std::tuple<std::decay_t<Value>>; };
template <>
struct return_tuple<void> { using type = std::tuple<>; };
template <typename Value>
using return_tuple_t = typename return_tuple<Value>::type;

template <typename Environment, typename Query, typename Value>
struct EnvironmentOverride
{
    Value query(Query) const noexcept { return m_value; }
    template <typename OtherQuery>
        requires (not std::same_as<Query, OtherQuery>) and requires(OtherQuery query, const Environment & environment) { query(environment); }
    auto query(OtherQuery query) const noexcept { return query(m_environment); }
    Environment m_environment;
    Value m_value;
};

template <typename Operation>
class OperationStorage
{
public:
    ~OperationStorage() noexcept { this->reset(); }
    OperationStorage() noexcept = default;
    OperationStorage(const OperationStorage &) = delete;
    OperationStorage & operator=(const OperationStorage &) = delete;
public:
    template <typename Factory>
    void emplace(Factory factory)
    {
        this->reset();
        ::new (static_cast<void *>(m_storage)) Operation(factory());
        m_initialized = true;
    }
    Operation & get() noexcept { return *std::launder(reinterpret_cast<Operation *>(m_storage)); }
    void reset() noexcept
    {
        if (m_initialized) { this->get().~Operation(); m_initialized = false; }
    }
private:
    alignas(Operation) std::byte m_storage[sizeof(Operation)];
    bool m_initialized = false;
};

class OperationRef
{
public:
    template <typename Operation>
    void bind(Operation & operation) noexcept
    {
        m_operation_p = &operation;
        m_start = [](void * operation_p) noexcept { exec::start(*static_cast<Operation *>(operation_p)); };
    }
    void start() const noexcept { m_start(m_operation_p); }
private:
    void * m_operation_p = nullptr;
    void (*m_start)(void *) noexcept = nullptr;
};

template <typename Owner, typename Environment, std::size_t Index = 0>
struct ChildReceiver
{
    using receiver_concept = receiver_t;
    Environment get_env() const noexcept { return m_environment; }
    template <typename... Values>
    void set_value(Values &&... values) noexcept { m_owner_p->template complete<Index>(exec::set_value, std::forward<Values>(values)...); }
    template <typename Error>
    void set_error(Error && error) noexcept { m_owner_p->template complete<Index>(exec::set_error, std::forward<Error>(error)); }
    void set_stopped() noexcept { m_owner_p->template complete<Index>(exec::set_stopped); }
    Owner * m_owner_p;
    Environment m_environment;
};

}
