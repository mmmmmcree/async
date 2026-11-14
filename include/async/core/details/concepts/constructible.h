#pragma once

#include <type_traits>

namespace lcf::async::details {

template <typename Type, typename... Arguments>
concept nothrow_constructible_from = std::is_nothrow_constructible_v<Type, Arguments...>;

template <typename... Types>
concept all_nothrow_default_constructible = (std::is_nothrow_default_constructible_v<Types> and ...);

template <typename... Types>
concept all_nothrow_copy_constructible = (std::is_nothrow_copy_constructible_v<Types> and ...);

template <typename... Types>
concept all_nothrow_move_constructible = (std::is_nothrow_move_constructible_v<Types> and ...);

} // namespace lcf::async::details
