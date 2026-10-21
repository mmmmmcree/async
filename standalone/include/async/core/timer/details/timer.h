#pragma once

#include <async/core/scheduling/details/callback.h>
#include <chrono>
#include <exception>
#include <limits>
#include <memory>
#include <memory_resource>
#include <utility>
#include <vector>

namespace lcf::async::details {

class TimerEntry
{
    using Self = TimerEntry;
    using EntryHandle = std::shared_ptr<Self>;
public:
    void configure(std::chrono::steady_clock::time_point deadline, Callback callback) noexcept { m_deadline = deadline; m_callback = callback; }
    auto getDeadline() const noexcept { return m_deadline; }
    std::size_t getHeapIndex() const noexcept { return m_index; }
    bool isQueued() const noexcept { return m_index != k_not_queued; }
    void setHeapIndex(std::size_t index) noexcept { m_index = index; }
    void resetHeapIndex() noexcept { m_index = k_not_queued; }
    bool isDecided() const noexcept { return m_decided; }
    bool isCancelled() const noexcept { return m_cancelled; }
    std::exception_ptr getException() const noexcept { return m_exception; }
    void markCancelled() noexcept { m_cancelled = true; }
    void setException(std::exception_ptr exception) noexcept { m_exception = std::move(exception); }
    void reject() noexcept { m_decided = true; m_cancelled = true; }
    Callback * claimCallback(EntryHandle owner_sp) noexcept
    {
        m_decided = true;
        m_ready_owner_sp = std::move(owner_sp);
        return &m_callback;
    }
    Callback * linkForAbandon(EntryHandle owner_sp, Callback * next_p) noexcept
    {
        this->resetHeapIndex();
        m_callback.m_next_p = next_p;
        return this->claimCallback(std::move(owner_sp));
    }
    auto releaseReadyOwner() noexcept { return std::move(m_ready_owner_sp); }
private:
    static constexpr std::size_t k_not_queued = std::numeric_limits<std::size_t>::max();
private:
    std::chrono::steady_clock::time_point m_deadline;
    Callback m_callback;
    EntryHandle m_ready_owner_sp;
    std::exception_ptr m_exception;
    std::size_t m_index = k_not_queued;
    bool m_decided = false;
    bool m_cancelled = false;
};

/* TimerService serializes the heap; no scheduling type sees its indices. */
class TimerQueue
{
    using Self = TimerQueue;
    using EntryHandle = std::shared_ptr<TimerEntry>;
    using Entries = std::pmr::vector<EntryHandle>;
public:
    explicit TimerQueue(std::pmr::memory_resource * resource_p) : m_entries(0, resource_p) {}
    TimerQueue(const Self &) = delete;
    Self & operator=(const Self &) = delete;
public:
    bool empty() const noexcept { return m_entries.empty(); }
    auto deadline() const noexcept { return m_entries.front()->getDeadline(); }
    bool insert(EntryHandle && entry_sp)
    {
        auto * entry_p = entry_sp.get();
        const auto index = m_entries.size();
        m_entries.push_back(std::move(entry_sp));
        m_entries.back()->setHeapIndex(index);
        this->siftUp(index);
        return entry_p->getHeapIndex() == 0;
    }
    EntryHandle remove(std::size_t index) noexcept
    {
        auto removed_sp = std::move(m_entries[index]);
        if (index + 1 == m_entries.size()) { m_entries.pop_back(); }
        else {
            m_entries[index] = std::move(m_entries.back());
            m_entries.pop_back();
            m_entries[index]->setHeapIndex(index);
            if (index > 0 and m_entries[index]->getDeadline() < m_entries[(index - 1) / 2]->getDeadline()) { this->siftUp(index); }
            else { this->siftDown(index); }
        }
        removed_sp->resetHeapIndex();
        return removed_sp;
    }
    std::size_t readyCount(std::chrono::steady_clock::time_point now) const noexcept
    {
        if (m_entries.empty()) { return 0; }
        return this->countReady(0, now);
    }
    Callback * release() noexcept
    {
        Callback * callbacks_p = nullptr;
        for (auto & entry_sp : m_entries) {
            auto * entry_p = entry_sp.get();
            callbacks_p = entry_p->linkForAbandon(std::move(entry_sp), callbacks_p);
        }
        m_entries.clear();
        return callbacks_p;
    }
private:
    void exchange(std::size_t first, std::size_t second) noexcept
    {
        std::swap(m_entries[first], m_entries[second]);
        m_entries[first]->setHeapIndex(first);
        m_entries[second]->setHeapIndex(second);
    }
    void siftUp(std::size_t index) noexcept
    {
        while (index > 0) {
            const auto parent = (index - 1) / 2;
            if (m_entries[parent]->getDeadline() <= m_entries[index]->getDeadline()) { break; }
            this->exchange(index, parent);
            index = parent;
        }
    }
    void siftDown(std::size_t index) noexcept
    {
        for (;;) {
            auto child = index * 2 + 1;
            if (child >= m_entries.size()) { return; }
            if (child + 1 < m_entries.size() and m_entries[child + 1]->getDeadline() < m_entries[child]->getDeadline()) { ++child; }
            if (m_entries[index]->getDeadline() <= m_entries[child]->getDeadline()) { return; }
            this->exchange(index, child);
            index = child;
        }
    }
    std::size_t countReady(std::size_t index, std::chrono::steady_clock::time_point now) const noexcept
    {
        if (index >= m_entries.size() or m_entries[index]->getDeadline() > now) { return 0; }
        return 1 + this->countReady(index * 2 + 1, now) + this->countReady(index * 2 + 2, now);
    }
private:
    Entries m_entries;
};

}
