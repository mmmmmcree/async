#pragma once

#include <type_traits>

namespace lcf::async::details {

template <typename Type, typename... Arguments>
concept nothrow_constructible_from_c = std::is_nothrow_constructible_v<Type, Arguments...>;

template <typename... Types>
concept all_nothrow_default_constructible_c = (std::is_nothrow_default_constructible_v<Types> and ...);

template <typename... Types>
concept all_nothrow_copy_constructible_c = (std::is_nothrow_copy_constructible_v<Types> and ...);

template <typename... Types>
concept all_nothrow_move_constructible_c = (std::is_nothrow_move_constructible_v<Types> and ...);

template <typename... Types>
concept all_nothrow_move_assignable_c = (std::is_nothrow_move_assignable_v<Types> and ...);

} // namespace lcf::async::details
