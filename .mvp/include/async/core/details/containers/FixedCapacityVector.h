#pragma once

#include <cassert>
#include <concepts>
#include <cstddef>
#include <functional>
#include <limits>
#include <memory>
#include <memory_resource>
#include <new>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace lcf::async::details {

template <std::destructible Value>
class FixedCapacityVector
{
public:
    using Allocator = std::pmr::polymorphic_allocator<Value>;
public:
    FixedCapacityVector() = delete;
    FixedCapacityVector(std::size_t capacity, Allocator allocator)
        : m_allocator(allocator), m_capacity(capacity)
    {
        if (capacity > std::numeric_limits<std::size_t>::max() / sizeof(Value)) {
            throw std::bad_array_new_length{};
        }
        if (capacity != 0) {
            m_data_p = m_allocator.allocate(capacity);
        }
    }

    FixedCapacityVector(const FixedCapacityVector &) = delete;
    FixedCapacityVector & operator=(const FixedCapacityVector &) = delete;
    FixedCapacityVector(FixedCapacityVector && other) noexcept
        : m_allocator(other.m_allocator),
          m_data_p(std::exchange(other.m_data_p, nullptr)),
          m_capacity(std::exchange(other.m_capacity, 0)),
          m_size(std::exchange(other.m_size, 0)) {}
    FixedCapacityVector & operator=(FixedCapacityVector &&) = delete;

    ~FixedCapacityVector() noexcept
    {
        while (m_size != 0) {
            std::destroy_at(m_data_p + --m_size);
        }
        if (m_data_p) {
            m_allocator.deallocate(m_data_p, m_capacity);
        }
    }

    template <typename... Args>
        requires std::constructible_from<Value, Args...>
    Value & emplaceBack(Args &&... args)
    {
        this->checkCapacity();
        auto * value_p = std::construct_at(m_data_p + m_size, std::forward<Args>(args)...);
        ++m_size;
        return *value_p;
    }

    template <typename Factory, typename... Args>
        requires std::invocable<Factory, Args...>
            && std::same_as<std::invoke_result_t<Factory, Args...>, Value>
    Value & emplaceWith(Factory && factory, Args &&... args)
    {
        this->checkCapacity();
        // Placement new constructs in already allocated storage. Guaranteed
        // copy elision allows a factory to return a nonmovable Executor.
        auto * value_p = ::new (static_cast<void *>(m_data_p + m_size)) Value(
            std::invoke(std::forward<Factory>(factory), std::forward<Args>(args)...));
        ++m_size;
        return *value_p;
    }

    Value & operator[](std::size_t index) noexcept
    {
        assert(index < m_size);
        return m_data_p[index];
    }

    const Value & operator[](std::size_t index) const noexcept
    {
        assert(index < m_size);
        return m_data_p[index];
    }

    std::span<Value> values() noexcept { return {m_data_p, m_size}; }
    std::span<const Value> values() const noexcept { return {m_data_p, m_size}; }
    std::size_t size() const noexcept { return m_size; }
    std::size_t capacity() const noexcept { return m_capacity; }

private:
    void checkCapacity() const
    {
        if (m_size == m_capacity) {
            throw std::length_error{"FixedCapacityVector capacity exceeded"};
        }
    }

    Allocator m_allocator;
    Value * m_data_p = nullptr;
    std::size_t m_capacity;
    std::size_t m_size = 0;
};

} // namespace lcf::async::details
