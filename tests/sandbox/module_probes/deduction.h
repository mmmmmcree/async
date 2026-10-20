#pragma once

namespace dependency {

struct Just
{
    template <typename T> struct Sender;
    template <typename T> Sender(Just, T) -> Sender<T>;
    template <typename T> auto operator()(T value) const;
};

template <typename T>
struct Just::Sender
{
    Just m_tag;
    T m_value;
};

template <typename T>
auto Just::operator()(T value) const { return Just::Sender {Just {}, value}; }

inline constexpr Just k_just {};

}
