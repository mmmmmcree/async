#pragma once

#include <async/core/executable/declarations/concepts.h>
#include <async/core/scheduling/details/exec_schedule_adapters.h>
#include <async/core/scheduling/details/dispatch_targets.h>
#include <async/core/scheduling/declarations/concepts.h>
#include <async/core/scheduling/dispatcher/declarations/concepts.h>

#include <cstdint>

LCF_ASYNC_EXPORT
namespace lcf::async {

template <typename Submitter, cancellable_executable_c Executable>
class SchedulerHandle
{
public:
    using scheduler_concept = protocols::scheduler_t;
public:
    ~SchedulerHandle() noexcept = default;
    SchedulerHandle() = delete;
    explicit SchedulerHandle(Submitter & submitter) noexcept :
        m_submitter_p(&submitter),
        m_domain_id(details::k_unbound_domain_id),
        m_scheduler_index(details::k_owner_scheduler_index) {}
    SchedulerHandle(Submitter & submitter, std::uint32_t domain_id, std::uint32_t scheduler_index) noexcept :
        m_submitter_p(&submitter),
        m_domain_id(domain_id),
        m_scheduler_index(scheduler_index) {}
    SchedulerHandle(const SchedulerHandle &) noexcept = default;
    SchedulerHandle(SchedulerHandle &&) noexcept = default;
    SchedulerHandle & operator=(const SchedulerHandle &) noexcept = default;
    SchedulerHandle & operator=(SchedulerHandle &&) noexcept = default;
    bool operator==(const SchedulerHandle &) const noexcept = default;
public:
    auto schedule() const noexcept requires callback_executable_c<Executable>;
    template <typename Record>
    requires record_submitter_c<Submitter, Record>
    std::error_code submit(Record & record) const noexcept
    {
        if constexpr (dispatch_record_c<Record>) { record.setTargetIndex(m_scheduler_index); }
        return m_submitter_p->submit(record);
    }
private:
    Submitter * m_submitter_p;
    std::uint32_t m_domain_id;
    std::uint32_t m_scheduler_index;
};

template <typename Submitter, cancellable_executable_c Executable>
auto SchedulerHandle<Submitter, Executable>::schedule() const noexcept requires callback_executable_c<Executable>
{
    return details::ScheduleSender<SchedulerHandle, Executable>{*this};
}

} // namespace lcf::async
