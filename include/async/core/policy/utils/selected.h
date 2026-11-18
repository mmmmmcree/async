#pragma once

#include <async/core/policy/declarations/concepts.h>

#include <memory>
#include <ranges>
#include <utility>

LCF_ASYNC_EXPORT
namespace lcf::async::utils {

namespace details {

template <typename Policy>
class SelectedAdaptor : public std::ranges::range_adaptor_closure<SelectedAdaptor<Policy>>
{
    template <typename Range>
    using Selection = decltype(std::declval<Policy &>().select(std::declval<std::views::all_t<Range>>()));
public:
    ~SelectedAdaptor() noexcept = default;
    SelectedAdaptor() = delete;
    explicit SelectedAdaptor(Policy & policy) noexcept : m_policy_p(std::addressof(policy)) {}
    SelectedAdaptor(const SelectedAdaptor &) noexcept = default;
    SelectedAdaptor(SelectedAdaptor &&) noexcept = default;
    SelectedAdaptor & operator=(const SelectedAdaptor &) noexcept = default;
    SelectedAdaptor & operator=(SelectedAdaptor &&) noexcept = default;
public:
    template <std::ranges::viewable_range Range>
    requires selection_policy_c<Policy, std::views::all_t<Range>>
    Selection<Range> operator()(Range && candidates) const noexcept
    {
        return m_policy_p->select(std::views::all(std::forward<Range>(candidates)));
    }
private:
    Policy * m_policy_p;
};

template <std::ranges::viewable_range Range, typename Policy>
requires selection_policy_c<Policy, std::views::all_t<Range>>
auto operator|(Range && candidates, SelectedAdaptor<Policy> adaptor) noexcept
{
    return adaptor(std::forward<Range>(candidates));
}

} // namespace details

template <typename Policy>
details::SelectedAdaptor<Policy> selected(Policy & policy) noexcept { return details::SelectedAdaptor<Policy> {policy}; }

inline decltype(std::views::all) selected(NoSelectionPolicy) noexcept { return std::views::all; }

} // namespace lcf::async::utils
