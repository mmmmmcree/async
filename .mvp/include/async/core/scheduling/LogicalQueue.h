#pragma once

#include <async/core/concepts.h>

#include <cstdint>

namespace lcf::async {

class ContextInfo;

class LogicalQueueToken
{
    friend class ContextInfo;
public:
    ~LogicalQueueToken() noexcept = default;
    LogicalQueueToken() = delete;
    LogicalQueueToken(const LogicalQueueToken &) noexcept = default;
    LogicalQueueToken(LogicalQueueToken &&) noexcept = default;
    LogicalQueueToken & operator=(const LogicalQueueToken &) noexcept = default;
    LogicalQueueToken & operator=(LogicalQueueToken &&) noexcept = default;
private:
    LogicalQueueToken(std::uint32_t domain_id, std::uint32_t index) noexcept :
        m_domain_id(domain_id), m_index(index) {}
public:
    std::uint32_t getDomainId() const noexcept { return m_domain_id; }
    std::uint32_t getIndex() const noexcept { return m_index; }
    bool operator==(const LogicalQueueToken &) const noexcept = default;
private:
    std::uint32_t m_domain_id;
    std::uint32_t m_index;
};

template <typename Executor>
class LogicalQueue
{
public:
    ~LogicalQueue() noexcept = default;
    LogicalQueue() = delete;
    explicit LogicalQueue(Executor & executor) noexcept : m_executor_p(&executor) {}
    LogicalQueue(const LogicalQueue &) noexcept = default;
    LogicalQueue(LogicalQueue &&) noexcept = default;
    LogicalQueue & operator=(const LogicalQueue &) noexcept = default;
    LogicalQueue & operator=(LogicalQueue &&) noexcept = default;
public:
    template <typename Record>
    requires record_submitter_c<Executor, Record>
    EnqueueResult tryEnqueue(Record & record) const noexcept { return m_executor_p->tryEnqueue(record); }
private:
    Executor * m_executor_p;
};

} // namespace lcf::async
