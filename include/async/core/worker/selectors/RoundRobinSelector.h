#pragma once

#include <async/core/scheduling/declarations/concepts.h>

#include <algorithm>
#include <cstddef>
#include <span>
#include <type_traits>

LCF_ASYNC_EXPORT
namespace lcf::async {

template <work_executor_c SelectedExecutor>
class RoundRobinSelector
{
public:
    using Executor = SelectedExecutor;
private:
    using ExecutorView = std::span<Executor>;
public:
    ~RoundRobinSelector() noexcept = default;
    RoundRobinSelector() noexcept = default;
    explicit RoundRobinSelector(ExecutorView executors, std::size_t start_index = 0) noexcept :
        m_executors(executors), m_next_index(executors.empty() ? 0 : start_index % executors.size()) {}
    RoundRobinSelector(const RoundRobinSelector &) noexcept = default;
    RoundRobinSelector(RoundRobinSelector &&) noexcept = default;
    RoundRobinSelector & operator=(const RoundRobinSelector &) noexcept = default;
    RoundRobinSelector & operator=(RoundRobinSelector &&) noexcept = default;
public:
    void beginSelection() noexcept { m_remaining = m_executors.size(); }
    template <typename Visitor>
    requires std::is_nothrow_invocable_r_v<void, Visitor &, Executor &>
    bool visitNext(Visitor && visitor) noexcept
    {
        if (m_remaining == 0) { return false; }
        --m_remaining;
        auto * executor_p = &m_executors[m_next_index];
        if (++m_next_index == m_executors.size()) { m_next_index = 0; }
        visitor(*executor_p);
        return true;
    }
    bool isEmpty() const noexcept { return std::ranges::all_of(m_executors, &Executor::isEmpty); }
private:
    ExecutorView m_executors;
    std::size_t m_next_index = 0;
    std::size_t m_remaining = 0;
};

} // namespace lcf::async
