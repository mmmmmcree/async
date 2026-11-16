#pragma once

#include <async/core/stream/utils/pull_any.h>

#include <iterator>
#include <memory>
#include <ranges>
#include <type_traits>
#include <utility>

LCF_ASYNC_EXPORT
namespace lcf::async::utils {

namespace details {

template <typename Input>
class PullAllView : public std::ranges::view_interface<PullAllView<Input>>
{
    class Iterator
    {
    public:
        using iterator_concept = std::input_iterator_tag;
        using value_type = std::remove_pointer_t<decltype(utils::pull_any(std::declval<Input &>()))>;
        using difference_type = std::ptrdiff_t;
    public:
        ~Iterator() noexcept = default;
        Iterator() noexcept = default;
        explicit Iterator(Input & input) noexcept : m_input_p(&input) {}
        Iterator(const Iterator &) noexcept = default;
        Iterator(Iterator &&) noexcept = default;
        Iterator & operator=(const Iterator &) noexcept = default;
        Iterator & operator=(Iterator &&) noexcept = default;
    public:
        value_type & operator*() const noexcept { return *m_value_p; }
        Iterator & operator++() noexcept
        {
            m_value_p = utils::pull_any(*m_input_p);
            return *this;
        }
        void operator++(int) noexcept { ++*this; }
        bool operator==(std::default_sentinel_t) const noexcept { return m_value_p == nullptr; }
    private:
        Input * m_input_p = nullptr;
        value_type * m_value_p = nullptr;
    };
public:
    ~PullAllView() noexcept = default;
    PullAllView() noexcept = default;
    explicit PullAllView(Input input) noexcept(std::is_nothrow_move_constructible_v<Input>) : m_input(std::move(input)) {}
    PullAllView(const PullAllView &) = default;
    PullAllView(PullAllView &&) = default;
    PullAllView & operator=(const PullAllView &) = default;
    PullAllView & operator=(PullAllView &&) = default;
public:
    Iterator begin() noexcept { return std::move(++Iterator {m_input}); }
    std::default_sentinel_t end() const noexcept { return {}; }
private:
    Input m_input {};
};

} // namespace details

template <drainable_source_c Source>
requires (not std::is_pointer_v<std::remove_reference_t<Source>>)
details::PullAllView<source_object_t<Source> *> pull_all(Source & source) noexcept
{ return details::PullAllView<source_object_t<Source> *> {std::addressof(source)}; }

template <drainable_source_c Source>
requires (not std::is_pointer_v<Source>)
details::PullAllView<source_object_t<Source> *> pull_all(Source * source_p) noexcept { return pull_all(*source_p); }

template <drainable_source_range_c Sources>
requires std::ranges::viewable_range<Sources>
auto pull_all(Sources && sources) noexcept(noexcept(details::PullAllView {std::views::all(std::forward<Sources>(sources))}))
{
    return details::PullAllView {std::views::all(std::forward<Sources>(sources))};
}

} // namespace lcf::async::utils
