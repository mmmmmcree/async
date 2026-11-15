#pragma once

#include <async/config/macros.h>
#include <async/core/details/concepts/constructible.h>
#include <async/core/scheduling/dispatcher/declarations/concepts.h>
#include <async/core/scheduling/policy/declarations/concepts.h>
#include <async/core/scheduling/utils/selected.h>

#include <algorithm>
#include <span>
#include <utility>

LCF_ASYNC_EXPORT
namespace lcf::async {

template <request_dispatcher_c Dispatcher, typename SelectionPolicy>
requires selection_policy_c<SelectionPolicy, std::span<Dispatcher>>
class DispatcherGroup
{
    using DispatcherSpan = std::span<Dispatcher>;
public:
    ~DispatcherGroup() noexcept = default;
    DispatcherGroup() = delete;
    explicit DispatcherGroup(SelectionPolicy selection_policy) noexcept(
        details::all_nothrow_move_constructible<SelectionPolicy>) :
        m_selection_policy(std::move(selection_policy)) {}
    explicit DispatcherGroup(DispatcherSpan dispatchers) noexcept(
        details::all_nothrow_default_constructible<SelectionPolicy>)
    requires std::default_initializable<SelectionPolicy> :
        m_dispatchers(dispatchers), m_selection_policy() {}
    DispatcherGroup(const DispatcherGroup &) = delete;
    DispatcherGroup(DispatcherGroup &&) noexcept(
        details::all_nothrow_move_constructible<SelectionPolicy>) = default;
    DispatcherGroup & operator=(const DispatcherGroup &) = delete;
    DispatcherGroup & operator=(DispatcherGroup &&) = delete;
public:
    void attach(DispatcherSpan dispatchers) noexcept { m_dispatchers = dispatchers; }
    DispatcherGroup makeIndependent() const noexcept(
        details::all_nothrow_default_constructible<SelectionPolicy>)
    requires std::default_initializable<SelectionPolicy>
    {
        return DispatcherGroup {m_dispatchers};
    }
    DispatchReport dispatchOne() noexcept
    {
        for (Dispatcher & dispatcher : m_dispatchers | utils::selected(m_selection_policy)) {
            const DispatchReport report = dispatcher.dispatchOne();
            if (report.hasProgress() or report.m_error_code) { return report; }
        }
        return {};
    }
    bool isEmpty() const noexcept
    {
        return std::ranges::all_of(m_dispatchers, &Dispatcher::isEmpty);
    }
private:
    DispatcherSpan m_dispatchers;
    SelectionPolicy m_selection_policy;
};

} // namespace lcf::async
