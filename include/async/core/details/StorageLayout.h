#pragma once

#include <async/core/declarations/structs.h>

#include <cstddef>
#include <expected>
#include <limits>
#include <memory_resource>
#include <system_error>

namespace lcf::async::details {

class StorageLayout
{
public:
    ~StorageLayout() noexcept = default;
    StorageLayout() noexcept = default;
    StorageLayout(const StorageLayout &) noexcept = default;
    StorageLayout(StorageLayout &&) noexcept = default;
    StorageLayout & operator=(const StorageLayout &) noexcept = default;
    StorageLayout & operator=(StorageLayout &&) noexcept = default;
public:
    template <typename Value>
    void append(std::size_t count = 1) noexcept
    {
        if (count == 0 or m_overflow) { return; }
        constexpr std::size_t maximum = std::numeric_limits<std::size_t>::max();
        m_overflow = count > maximum / sizeof(Value);
        if (not m_overflow) { this->append({sizeof(Value) * count, alignof(Value)}); }
    }
    void append(StorageRequirements requirements) noexcept
    {
        if (requirements.m_size_in_bytes == 0 or m_overflow) { return; }
        constexpr std::size_t maximum = std::numeric_limits<std::size_t>::max();
        const auto [bytes, alignment] = requirements;
        const std::size_t padding = (alignment - m_size % alignment) % alignment;
        m_overflow = m_size > maximum - padding or bytes > maximum - padding - m_size;
        if (m_overflow) { return; }
        m_size += padding + bytes;
        if (alignment > m_alignment) { m_alignment = alignment; }
    }
    std::expected<StorageRequirements, std::error_code> getSize() const noexcept
    {
        if (m_overflow) { return std::unexpected {std::make_error_code(std::errc::value_too_large)}; }
        return StorageRequirements {.m_size_in_bytes = m_size, .m_alignment = m_alignment};
    }
    template <typename Container>
    void reserve(const Container &, std::size_t count) noexcept { this->append<typename Container::value_type>(count); }
private:
    std::size_t m_size = 0;
    std::size_t m_alignment = 1;
    bool m_overflow = false;
};

class StorageAllocator
{
public:
    ~StorageAllocator() noexcept = default;
    StorageAllocator() = delete;
    explicit StorageAllocator(std::pmr::memory_resource & resource) noexcept : m_resource_p(&resource) {}
    StorageAllocator(const StorageAllocator &) noexcept = default;
    StorageAllocator(StorageAllocator &&) noexcept = default;
    StorageAllocator & operator=(const StorageAllocator &) noexcept = default;
    StorageAllocator & operator=(StorageAllocator &&) noexcept = default;
public:
    template <typename Container>
    void reserve(Container & container, std::size_t count) const { container = {count, {m_resource_p}}; }
private:
    std::pmr::memory_resource * m_resource_p;
};

} // namespace lcf::async::details
