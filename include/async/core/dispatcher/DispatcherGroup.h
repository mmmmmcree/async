#pragma once

#include <async/config/macros.h>
#include <async/core/dispatcher/declarations/concepts.h>
#include <async/core/policy/utils/selected.h>

#include <ranges>
#include <span>

LCF_ASYNC_EXPORT
namespace lcf::async {

template <request_dispatcher_c Dispatcher, typename SelectionPolicy>
class DispatcherGroup
{
public:
    ~DispatcherGroup() noexcept = default;
    DispatcherGroup() = delete;
    explicit DispatcherGroup(std::span<Dispatcher> dispatchers, SelectionPolicy policy = {}) :
        m_dispatchers(dispatchers), m_policy(std::move(policy)) {}
    DispatcherGroup(const DispatcherGroup &) = default;
    DispatcherGroup(DispatcherGroup &&) = default;
    DispatcherGroup & operator=(const DispatcherGroup &) = default;
    DispatcherGroup & operator=(DispatcherGroup &&) = default;
public:
    DispatchReport dispatchOne() noexcept
    {
        for (Dispatcher & dispatcher : m_dispatchers | utils::selected(m_policy)) {
            const DispatchReport report = dispatcher.dispatchOne();
            if (report.hasProgress() or report.m_error_code) { return report; }
        }
        return {};
    }
private:
    std::span<Dispatcher> m_dispatchers;
    SelectionPolicy m_policy;
};

} // namespace lcf::async
