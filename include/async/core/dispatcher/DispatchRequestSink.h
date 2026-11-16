#pragma once

#include <async/core/sink/declarations/concepts.h>
#include <async/core/scheduling/declarations/enums.h>
#include <async/core/policy/declarations/concepts.h>

#include <ranges>
#include <span>
#include <utility>

LCF_ASYNC_EXPORT
namespace lcf::async {

template <rejecting_sink_c Target, typename SelectionPolicy>
requires selection_policy_c<SelectionPolicy, std::span<Target>>
class DispatchRequestSink
{
public:
    using value_t = typename Target::value_t;
public:
    ~DispatchRequestSink() noexcept = default;
    DispatchRequestSink() = delete;
    DispatchRequestSink(std::span<Target *> targets, std::span<Target> shared_targets, SelectionPolicy policy = {}) :
        m_targets(targets), m_shared_targets(shared_targets), m_policy(std::move(policy)) {}
    DispatchRequestSink(const DispatchRequestSink &) = default;
    DispatchRequestSink(DispatchRequestSink &&) = default;
    DispatchRequestSink & operator=(const DispatchRequestSink &) = default;
    DispatchRequestSink & operator=(DispatchRequestSink &&) = default;
public:
    std::error_code receive(value_t & record) noexcept
    {
        const std::size_t index = record.getSchedulerIndex();
        if (index >= m_targets.size()) { return std::make_error_code(std::errc::invalid_argument); }
        if (Target * target_p = m_targets[index]) { return target_p->receive(record); }
        for (Target & target : m_policy.select(std::views::all(m_shared_targets))) {
            const std::error_code ec = target.receive(record);
            if (ec != std::errc::no_buffer_space) { return ec; }
        }
        return std::make_error_code(std::errc::no_buffer_space);
    }
private:
    std::span<Target *> m_targets;
    std::span<Target> m_shared_targets;
    SelectionPolicy m_policy;
};

} // namespace lcf::async
