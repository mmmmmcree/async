#pragma once

#include <cstddef>
#include <memory>
#include <new>
#include <optional>
#include <type_traits>

namespace lcf::async::details {

template <typename T>
struct ReturnStorage
{
    static constexpr std::size_t k_alignment = alignof(T);
#if defined(__clang__) and defined(_MSC_VER)
    // MSVC's coroutine_handle passes zero promise alignment to Clang's builtin.
    static constexpr bool k_indirect_value = alignof(T) > __STDCPP_DEFAULT_NEW_ALIGNMENT__;
#else
    static constexpr bool k_indirect_value = false;
#endif
    using Storage = std::conditional_t<k_indirect_value, std::unique_ptr<T>, std::optional<T>>;
    void return_value(T value) noexcept(not k_indirect_value and std::is_nothrow_move_constructible_v<T>)
    {
        if constexpr (k_indirect_value) { m_value = std::make_unique<T>(std::move(value)); }
        else { m_value.emplace(std::move(value)); }
    }
    T takeValue() noexcept(std::is_nothrow_move_constructible_v<T>) { return std::move(*m_value); }
    Storage m_value;
};

template <>
struct ReturnStorage<void>
{
    static constexpr std::size_t k_alignment = __STDCPP_DEFAULT_NEW_ALIGNMENT__;
    void return_void() noexcept {}
    void takeValue() noexcept {}
};

}
