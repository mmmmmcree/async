#pragma once

#include <async/core/sink/declarations/concepts.h>

#include <memory>
#include <system_error>

LCF_ASYNC_EXPORT
namespace lcf::async {

template <typename Record>
class AnySinkRef
{
public:
    using value_t = Record;
public:
    ~AnySinkRef() noexcept = default;
    AnySinkRef() = delete;
    template <rejecting_sink_c Sink>
    requires (not std::same_as<Sink, AnySinkRef>) and std::same_as<typename Sink::value_t, Record>
    explicit AnySinkRef(Sink & sink) noexcept : m_sink_p(std::addressof(sink)),
        m_receive_f([](void * state_p, Record & record) noexcept { return static_cast<Sink *>(state_p)->receive(record); }) {}
    AnySinkRef(const AnySinkRef &) noexcept = default;
    AnySinkRef(AnySinkRef &&) noexcept = default;
    AnySinkRef & operator=(const AnySinkRef &) noexcept = default;
    AnySinkRef & operator=(AnySinkRef &&) noexcept = default;
    std::error_code receive(Record & record) noexcept { return m_receive_f(m_sink_p, record); }
private:
    void * m_sink_p;
    std::error_code (*m_receive_f)(void *, Record &) noexcept;
};

} // namespace lcf::async
