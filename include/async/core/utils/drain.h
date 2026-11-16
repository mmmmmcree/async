#pragma once

#include <async/core/stream/declarations/concepts.h>

#include <iterator>
#include <ranges>

LCF_ASYNC_EXPORT
namespace lcf::async::utils {

namespace details {

template <typename Stream>
class DrainView : public std::ranges::view_interface<DrainView<Stream>>
{
    class Iterator
    {
    public:
        using iterator_concept = std::input_iterator_tag;
        using value_type = typename Stream::value_t;
        using difference_type = std::ptrdiff_t;
    public:
        ~Iterator() noexcept = default;
        Iterator() noexcept = default;
        explicit Iterator(Stream & stream) noexcept : m_stream_p(&stream) {}
        Iterator(const Iterator &) noexcept = default;
        Iterator(Iterator &&) noexcept = default;
        Iterator & operator=(const Iterator &) noexcept = default;
        Iterator & operator=(Iterator &&) noexcept = default;
    public:
        value_type & operator*() const noexcept { return *m_value_p; }
        Iterator & operator++() noexcept
        {
            m_value_p = m_stream_p->pullRemaining();
            return *this;
        }
        void operator++(int) noexcept { ++*this; }
        bool operator==(std::default_sentinel_t) const noexcept { return m_value_p == nullptr; }
    private:
        Stream * m_stream_p = nullptr;
        value_type * m_value_p = nullptr;
    };
public:
    ~DrainView() noexcept = default;
    DrainView() noexcept = default;
    explicit DrainView(Stream & stream) noexcept : m_stream_p(&stream) {}
    DrainView(const DrainView &) noexcept = default;
    DrainView(DrainView &&) noexcept = default;
    DrainView & operator=(const DrainView &) noexcept = default;
    DrainView & operator=(DrainView &&) noexcept = default;
public:
    Iterator begin() const noexcept { return std::move(++Iterator {*m_stream_p}); }
    std::default_sentinel_t end() const noexcept { return {}; }
private:
    Stream * m_stream_p = nullptr;
};

} // namespace details

template <remaining_stream_c Stream>
details::DrainView<Stream> drain(Stream & stream) noexcept { return details::DrainView<Stream> {stream}; }

} // namespace lcf::async::utils
