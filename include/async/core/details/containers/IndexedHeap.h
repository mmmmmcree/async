#pragma once

#include <async/core/details/containers/FixedCapacityList.h>

#include <concepts>
#include <limits>
#include <memory>
#include <system_error>
#include <utility>

namespace lcf::async::details {

// Values have stable addresses, outlive the heap, and start with position npos.
// Compare orders values; Position exposes their index. Each value belongs to one heap.
template <typename Value, typename Compare, typename Position>
requires all_nothrow_move_constructible_c<Compare, Position> and all_nothrow_move_assignable_c<Compare, Position>
    and requires(Compare & compare, Position & position, Value & value, const Value & left, const Value & right) {
        { compare(left, right) } noexcept -> std::same_as<bool>;
        { position(value) } noexcept -> std::same_as<std::size_t &>;
    }
class IndexedHeap
{
public:
    using value_type = Value *;
    using size_type = std::size_t;
    using allocator_type = std::pmr::polymorphic_allocator<value_type>;
    static constexpr size_type npos = std::numeric_limits<size_type>::max();
public:
    ~IndexedHeap() noexcept { this->clear(); }
    IndexedHeap() noexcept(all_nothrow_default_constructible_c<Compare, Position>) = default;
    IndexedHeap(size_type capacity, allocator_type allocator, Compare compare = {}, Position position = {}) :
        m_values(capacity, allocator), m_compare(std::move(compare)), m_position(std::move(position)) {}
    IndexedHeap(const IndexedHeap &) = delete;
    IndexedHeap(IndexedHeap &&) noexcept = default;
    IndexedHeap & operator=(const IndexedHeap &) = delete;
    IndexedHeap & operator=(IndexedHeap && other) noexcept
    {
        if (this != &other) {
            this->clear();
            m_values = std::move(other.m_values);
            m_compare = std::move(other.m_compare);
            m_position = std::move(other.m_position);
        }
        return *this;
    }
public:
    bool empty() const noexcept { return m_values.empty(); }
    size_type size() const noexcept { return m_values.size(); }
    size_type capacity() const noexcept { return m_values.capacity(); }
    Value * top() const noexcept { return this->empty() ? nullptr : m_values.front(); }
    std::error_code insert_or_update(Value & value) noexcept
    {
        size_type & index = m_position(value);
        if (index == npos) {
            if (this->size() == this->capacity()) { return std::make_error_code(std::errc::no_buffer_space); }
            index = this->size();
            m_values.emplace_back(std::addressof(value));
        }
        this->repair(index);
        return {};
    }
    void erase(Value & value) noexcept
    {
        const size_type index = m_position(value);
        if (index == npos) { return; }
        this->swap_values(index, this->size() - 1);
        m_values.pop_back();
        m_position(value) = npos;
        if (index < this->size()) { this->repair(index); }
    }
    Value * pop() noexcept
    {
        Value * value_p = this->top();
        if (value_p) { this->erase(*value_p); }
        return value_p;
    }
    void clear() noexcept
    {
        for (Value * value_p : m_values) { m_position(*value_p) = npos; }
        m_values.clear();
    }
private:
    bool comes_before(size_type left, size_type right) noexcept
    { return m_compare(std::as_const(*m_values[left]), std::as_const(*m_values[right])); }
    void swap_values(size_type left, size_type right) noexcept
    {
        std::swap(m_values[left], m_values[right]);
        m_position(*m_values[left]) = left;
        m_position(*m_values[right]) = right;
    }
    void repair(size_type index) noexcept
    {
        while (index) {
            const size_type parent = (index - 1) / 2;
            if (not this->comes_before(index, parent)) { break; }
            this->swap_values(index, parent);
            index = parent;
        }
        while (index < this->size() / 2) {
            size_type child = 2 * index + 1;
            if (child + 1 < this->size() and this->comes_before(child + 1, child)) { ++child; }
            if (not this->comes_before(child, index)) { break; }
            this->swap_values(index, child);
            index = child;
        }
    }
private:
    FixedCapacityList<value_type> m_values;
    Compare m_compare;
    Position m_position;
};

} // namespace lcf::async::details
