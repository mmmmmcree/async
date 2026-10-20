#pragma once

#include <async/core/protocol/details/utility.h>

namespace lcf::async::exec::details {

template <typename Tag, typename Tuple>
struct Completion
{
    template <typename Receiver>
    void deliver(Receiver receiver) && noexcept
    { std::apply([&](auto &&... values) noexcept { Tag {}(std::move(receiver), std::forward<decltype(values)>(values)...); }, std::move(m_values)); }
    template <typename Receiver>
    void replay(Receiver receiver) const noexcept
    { std::apply([&](const auto &... values) noexcept { Tag {}(std::move(receiver), values...); }, m_values); }
    Tuple m_values;
};
template <typename List>
struct as_variant;
template <typename... Types>
struct as_variant<type_list<Types...>> { using type = std::variant<std::monostate, Types...>; };
template <typename Signatures>
struct completion_variant;
template <typename... Signatures>
struct completion_variant<completion_signatures<Signatures...>>
{
    using list = typename unique_list<type_list<Completion<set_error_t, std::tuple<std::exception_ptr>>, Completion<set_stopped_t, std::tuple<>>>,
        Completion<typename signature_traits<Signatures>::tag, typename signature_traits<Signatures>::tuple>...>::type;
    using type = typename as_variant<list>::type;
};

// The old task result placeholder becomes a cache of protocol completion channels.
template <typename Signatures>
class CompletionStorage
{
    using Storage = typename completion_variant<Signatures>::type;
public:
    template <typename Tag, typename... Values>
    void publish(Tag, Values &&... values) noexcept
    {
        using Record = Completion<Tag, std::tuple<std::decay_t<Values>...>>;
        try { m_completion.template emplace<Record>(Record {{std::forward<Values>(values)...}}); }
        catch (...) {
            using Error = Completion<set_error_t, std::tuple<std::exception_ptr>>;
            m_completion.template emplace<Error>(Error {{std::current_exception()}});
        }
    }
    template <typename Receiver>
    void deliver(Receiver receiver) noexcept
    {
        try {
            auto stored = std::move(m_completion);
            std::visit([&](auto & completion) noexcept {
                if constexpr (not std::same_as<std::remove_cvref_t<decltype(completion)>, std::monostate>) { std::move(completion).deliver(std::move(receiver)); }
            }, stored);
        } catch (...) { set_error(std::move(receiver), std::current_exception()); }
    }
    template <typename Receiver>
    void replay(Receiver receiver) const noexcept
    {
        std::visit([&](const auto & completion) noexcept {
            if constexpr (not std::same_as<std::remove_cvref_t<decltype(completion)>, std::monostate>) { completion.replay(std::move(receiver)); }
        }, m_completion);
    }
private:
    Storage m_completion;
};

}
