#pragma once

#include <async/core/executable/declarations/concepts.h>
#include <async/core/scheduling/declarations/enums.h>

#include <cstdint>

LCF_ASYNC_EXPORT
namespace lcf::async {

template <cancellable_executable_c Executable>
class SchedulingRecord
{
public:
    using executable_t = Executable;
public:
    ~SchedulingRecord() noexcept = default;
    SchedulingRecord() = delete;
    explicit SchedulingRecord(Executable & executable) noexcept : m_executable_p(&executable) {}
    SchedulingRecord(const SchedulingRecord &) = delete;
    SchedulingRecord(SchedulingRecord &&) = delete;
    SchedulingRecord & operator=(const SchedulingRecord &) = delete;
    SchedulingRecord & operator=(SchedulingRecord &&) = delete;
    Executable & getExecutable() const noexcept { return *m_executable_p; }
    std::uint32_t getSchedulerIndex() const noexcept { return m_scheduler_index; }
    Priority getPriority() const noexcept { return m_priority; }
    void setSchedulerIndex(std::uint32_t index) noexcept { m_scheduler_index = index; }
    void setPriority(Priority priority) noexcept { m_priority = priority; }
    SchedulingRecord * getNext() const noexcept { return m_next_p; }
    void setNext(SchedulingRecord * next_p) noexcept { m_next_p = next_p; }
private:
    Executable * m_executable_p;
    SchedulingRecord * m_next_p = nullptr;
    std::uint32_t m_scheduler_index = 0;
    Priority m_priority = Priority::eNormal;
};

} // namespace lcf::async
