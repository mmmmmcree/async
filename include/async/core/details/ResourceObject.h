#pragma once
#include <memory>
#include <memory_resource>
#include <utility>
namespace lcf::async::details {
template <typename Value>
class ResourceObject
{
public:
    ~ResourceObject() noexcept { this->reset(); }
    ResourceObject() noexcept = default;
    ResourceObject(const ResourceObject &) = delete;
    ResourceObject(ResourceObject && other) noexcept :
        m_value_p(std::exchange(other.m_value_p, nullptr)), m_resource_p(other.m_resource_p) {}
    ResourceObject & operator=(const ResourceObject &) = delete;
    ResourceObject & operator=(ResourceObject && other) noexcept
    {
        if (this != &other) { this->reset(); m_value_p = std::exchange(other.m_value_p, nullptr); m_resource_p = other.m_resource_p; }
        return *this;
    }
    template <typename... Args>
    explicit ResourceObject(std::pmr::memory_resource & resource, Args &&... args) : m_resource_p(&resource)
    {
        std::pmr::polymorphic_allocator<Value> allocator {&resource};
        Value * value_p = allocator.allocate(1);
        try { std::construct_at(value_p, std::forward<Args>(args)...); }
        catch (...) { allocator.deallocate(value_p, 1); throw; }
        m_value_p = value_p;
    }
    Value * operator->() const noexcept { return m_value_p; }
    Value & operator*() const noexcept { return *m_value_p; }
    explicit operator bool() const noexcept { return m_value_p != nullptr; }
    void reset() noexcept
    {
        if (not m_value_p) { return; }
        std::destroy_at(m_value_p);
        std::pmr::polymorphic_allocator<Value> {m_resource_p}.deallocate(m_value_p, 1);
        m_value_p = nullptr;
    }
private:
    Value * m_value_p = nullptr;
    std::pmr::memory_resource * m_resource_p = std::pmr::null_memory_resource();
};
}
