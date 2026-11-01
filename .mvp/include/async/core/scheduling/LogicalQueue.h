#pragma once

#include "../operations.h"

#include <cstdint>

namespace lcf::async {

class ContextInfo;

class LogicalQueueToken
{
public:
    LogicalQueueToken(const LogicalQueueToken &) noexcept = default;
    LogicalQueueToken & operator=(const LogicalQueueToken &) noexcept = default;

    std::uint64_t getDomainId() const noexcept { return m_domain_id; }
    std::uint32_t getIndex() const noexcept { return m_index; }
    bool operator==(const LogicalQueueToken &) const noexcept = default;

private:
    // Issuance is restricted to successful ContextInfo registration.
    friend class ContextInfo;

    LogicalQueueToken(std::uint64_t domain_id, std::uint32_t index) noexcept
        : m_domain_id(domain_id), m_index(index) {}

    std::uint64_t m_domain_id;
    std::uint32_t m_index;
};

template <typename Executor>
class LogicalQueue
{
public:
    explicit LogicalQueue(Executor & executor) noexcept : m_executor_p(&executor) {}

    template <typename Record>
        requires executor_c<Executor, Record>
    EnqueueResult tryEnqueue(Record & record) const noexcept
    {
        return try_enqueue(*m_executor_p, record);
    }

private:
    Executor * m_executor_p;
};

} // namespace lcf::async
