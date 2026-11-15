#pragma once

#include <async/core/details/concepts/constructible.h>
#include <async/core/queue/declarations/concepts.h>
#include <async/core/scheduling/dispatcher/declarations/concepts.h>
#include <async/core/scheduling/dispatcher/declarations/structs.h>

#include <atomic>
#include <cstdint>
#include <system_error>
#include <utility>

LCF_ASYNC_EXPORT
namespace lcf::async {

template <consumer_queue_c RequestStream, producer_queue_c RequestSink>
requires work_record_c<typename RequestStream::value_t> and linked_node_c<typename RequestStream::value_t>
    and std::same_as<typename RequestStream::value_t, typename RequestSink::value_t>
    and requires(RequestStream & stream, const RequestStream & const_stream) {
        { stream.getReceivedCount() } noexcept -> std::same_as<std::uint64_t>;
        { stream.cancelPending() } noexcept -> std::same_as<void>;
        { stream.requeue(std::declval<typename RequestStream::value_t &>()) } noexcept -> std::same_as<void>;
        { const_stream.isEmpty() } noexcept -> std::same_as<bool>;
    }
class BasicDispatcher
{
    using DispatchRequest = typename RequestStream::value_t;
public:
    ~BasicDispatcher() noexcept = default;
    BasicDispatcher() = delete;
    BasicDispatcher(RequestStream request_stream, RequestSink request_sink) noexcept(
        details::all_nothrow_move_constructible<RequestStream, RequestSink>) :
        m_request_stream(std::move(request_stream)), m_request_sink(std::move(request_sink)) {}
    BasicDispatcher(const BasicDispatcher &) = delete;
    BasicDispatcher(BasicDispatcher &&) = delete;
    BasicDispatcher & operator=(const BasicDispatcher &) = delete;
    BasicDispatcher & operator=(BasicDispatcher &&) = delete;
public:
    DispatchReport dispatchOne() noexcept
    {
        if (m_dispatching.test_and_set(std::memory_order_acquire)) { return {}; }
        const std::uint64_t received_before = m_request_stream.getReceivedCount();
        DispatchRequest * request_p = m_request_stream.tryPop();
        if (not request_p) { m_dispatching.clear(std::memory_order_release); return {}; }
        const std::error_code ec = m_request_sink.tryEnqueue(*request_p);
        if (ec) { m_request_stream.requeue(*request_p); }
        const std::uint64_t received_count = m_request_stream.getReceivedCount() - received_before;
        m_dispatching.clear(std::memory_order_release);
        return {
            .m_received = received_count,
            .m_dispatched = not ec,
            .m_error_code = ec == std::errc::no_buffer_space ? std::error_code {} : ec
        };
    }
    bool isEmpty() const noexcept { return m_request_stream.isEmpty(); }
    void cancelPending() noexcept { m_request_stream.cancelPending(); }
private:
    RequestStream m_request_stream;
    RequestSink m_request_sink;
    std::atomic_flag m_dispatching {};
};

} // namespace lcf::async
