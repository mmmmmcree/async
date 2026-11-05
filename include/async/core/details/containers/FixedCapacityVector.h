#pragma once

#include <cassert>
#include <concepts>
#include <cstddef>
#include <iterator>
#include <limits>
#include <memory>
#include <memory_resource>
#include <span>
#include <type_traits>
#include <utility>

namespace lcf::async::details {

template <std::destructible Value>
class FixedCapacityVector
{
public:
    using value_type = Value;
    using allocator_type = std::pmr::polymorphic_allocator<Value>;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;
    using reference = Value &;
    using const_reference = const Value &;
    using pointer = Value *;
    using const_pointer = const Value *;
    using iterator = typename std::span<Value>::iterator;
    using const_iterator = typename std::span<const Value>::iterator;
    using reverse_iterator = std::reverse_iterator<iterator>;
    using const_reverse_iterator = std::reverse_iterator<const_iterator>;
public:
    ~FixedCapacityVector() noexcept { this->release_storage(); }
    FixedCapacityVector() noexcept = default;
    FixedCapacityVector(size_type capacity, allocator_type allocator) :
        m_resource_p(allocator.resource()), m_capacity(capacity)
    {
        if (capacity != 0) { m_elements = {allocator.allocate(capacity), size_type{0}}; }
    }
    FixedCapacityVector(const FixedCapacityVector &) = delete;
    FixedCapacityVector & operator=(const FixedCapacityVector &) = delete;
    FixedCapacityVector(FixedCapacityVector && other) noexcept :
        m_resource_p(other.m_resource_p),
        m_elements(std::exchange(other.m_elements, {})),
        m_capacity(std::exchange(other.m_capacity, 0)) {}
    FixedCapacityVector & operator=(FixedCapacityVector && other) noexcept
    {
        if (this == &other) { return *this; }
        this->release_storage();
        m_resource_p = other.m_resource_p;
        m_elements = std::exchange(other.m_elements, {});
        m_capacity = std::exchange(other.m_capacity, 0);
        return *this;
    }
public:
    reference operator[](size_type index) noexcept { return m_elements[index]; }
    const_reference operator[](size_type index) const noexcept { return m_elements[index]; }
    reference front() noexcept { return (*this)[0]; }
    const_reference front() const noexcept { return (*this)[0]; }
    reference back() noexcept { return (*this)[this->size() - 1]; }
    const_reference back() const noexcept { return (*this)[this->size() - 1]; }
    pointer data() noexcept { return m_elements.data(); }
    const_pointer data() const noexcept { return m_elements.data(); }
public:
    iterator begin() noexcept { return m_elements.begin(); }
    const_iterator begin() const noexcept { return std::span<const Value>{m_elements}.begin(); }
    const_iterator cbegin() const noexcept { return this->begin(); }
    iterator end() noexcept { return m_elements.end(); }
    const_iterator end() const noexcept { return std::span<const Value>{m_elements}.end(); }
    const_iterator cend() const noexcept { return this->end(); }
    reverse_iterator rbegin() noexcept { return reverse_iterator{this->end()}; }
    const_reverse_iterator rbegin() const noexcept { return const_reverse_iterator{this->end()}; }
    const_reverse_iterator crbegin() const noexcept { return this->rbegin(); }
    reverse_iterator rend() noexcept { return reverse_iterator{this->begin()}; }
    const_reverse_iterator rend() const noexcept { return const_reverse_iterator{this->begin()}; }
    const_reverse_iterator crend() const noexcept { return this->rend(); }
public:
    bool empty() const noexcept { return m_elements.empty(); }
    size_type size() const noexcept { return m_elements.size(); }
    size_type capacity() const noexcept { return m_capacity; }
    size_type max_size() const noexcept { return std::numeric_limits<size_type>::max() / sizeof(Value); }
    allocator_type get_allocator() const noexcept { return allocator_type{m_resource_p}; }
public:
    template <typename... Args>
    requires std::constructible_from<Value, Args...>
    reference emplace_back(Args &&... args) noexcept(std::is_nothrow_constructible_v<Value, Args...>)
    {
        assert(this->size() < m_capacity);
        auto * value_p = std::construct_at(this->data() + this->size(), std::forward<Args>(args)...);
        m_elements = {this->data(), this->size() + 1};
        return *value_p;
    }
    void pop_back() noexcept
    {
        assert(not this->empty());
        const auto new_size = this->size() - 1;
        m_elements = m_elements.first(new_size);
        if constexpr (not std::is_trivially_destructible_v<Value>) {
            std::destroy_at(this->data() + new_size);
        }
    }
    void clear() noexcept
    {
        if constexpr (std::is_trivially_destructible_v<Value>) {
            m_elements = m_elements.first(0);
        } else {
            while (not this->empty()) { this->pop_back(); }
        }
    }
    void swap(FixedCapacityVector & other) noexcept
    {
        std::swap(m_resource_p, other.m_resource_p);
        std::swap(m_elements, other.m_elements);
        std::swap(m_capacity, other.m_capacity);
    }
    friend void swap(FixedCapacityVector & lhs, FixedCapacityVector & rhs) noexcept { lhs.swap(rhs); }
private:
    void release_storage() noexcept
    {
        this->clear();
        if (this->data()) { this->get_allocator().deallocate(this->data(), m_capacity); }
    }
private:
    std::pmr::memory_resource * m_resource_p = std::pmr::null_memory_resource();
    std::span<Value> m_elements;
    std::size_t m_capacity = 0;
};

} // namespace lcf::async::details
