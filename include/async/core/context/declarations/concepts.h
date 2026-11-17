#pragma once

#include <async/core/dispatcher/declarations/concepts.h>
#include <async/core/notify/EventPumpSignal.h>
#include <async/core/notify/SignalNotifier.h>
#include <async/core/notify/declarations/concepts.h>
#include <async/core/queue/declarations/concepts.h>
#include <async/core/scheduling/declarations/concepts.h>
#include <async/core/sink/declarations/concepts.h>
#include <async/core/stream/declarations/concepts.h>
#include <async/core/worker/thread/declarations/concepts.h>

#include <span>

namespace lcf::async::details {

// Substitute the component factories before checking their protocols and connections.
template <typename Components, typename Notifier,
    typename Record = typename Components::record_t,
    typename SubmissionQueue = typename Components::submission_queue_t,
    typename ExecutionQueue = typename Components::execution_queue_t,
    typename Submission = typename Components::template submission_sink_t<SubmissionQueue, Notifier>,
    typename Target = typename Components::template submission_sink_t<ExecutionQueue, Notifier>,
    typename RequestBand = typename Components::template request_stream_t<SubmissionQueue>,
    typename ExecutionBand = typename Components::template execution_stream_t<ExecutionQueue>,
    typename RequestStream = typename Components::template priority_stream_t<RequestBand>,
    typename ExecutionStream = typename Components::template priority_stream_t<ExecutionBand>,
    typename DispatchSink = typename Components::template dispatch_sink_t<Target>,
    typename ExecuteSink = typename Components::template execute_sink_t<Record>,
    typename CancelSink = typename Components::template cancel_sink_t<Record>,
    typename Dispatcher = typename Components::template dispatcher_t<RequestStream, DispatchSink>,
    typename Executor = typename Components::template executor_t<ExecutionStream, ExecuteSink>,
    typename DispatchingExecutor = typename Components::template dispatching_executor_t<Dispatcher, Executor>>
concept context_component_assembly_c = requires(Submission & submission, Target & target, std::size_t band) {
    requires std::default_initializable<SubmissionQueue> and std::default_initializable<ExecutionQueue>;
    requires queue_c<SubmissionQueue> and queue_c<ExecutionQueue>;
    requires SubmissionQueue::k_supports_multiple_producers and SubmissionQueue::k_supports_multiple_consumers;
    requires ExecutionQueue::k_supports_multiple_producers and ExecutionQueue::k_supports_multiple_consumers;
    requires std::same_as<typename SubmissionQueue::value_t, Record> and std::same_as<typename ExecutionQueue::value_t, Record>;
    requires rejecting_sink_c<Submission> and rejecting_sink_c<Target> and rejecting_sink_c<DispatchSink>;
    requires std::same_as<typename Submission::value_t, Record> and std::same_as<typename Target::value_t, Record>
        and std::same_as<typename DispatchSink::value_t, Record>;
    requires std::constructible_from<Submission, Notifier> and std::constructible_from<Target, Notifier>;
    { submission.getQueue(band) } noexcept -> std::same_as<SubmissionQueue &>;
    { target.getQueue(band) } noexcept -> std::same_as<ExecutionQueue &>;
    requires std::constructible_from<RequestBand, std::span<SubmissionQueue *>>;
    requires std::constructible_from<ExecutionBand, std::span<ExecutionQueue *>, std::size_t>;
    requires std::constructible_from<RequestStream, std::span<RequestBand>>
        and std::constructible_from<ExecutionStream, std::span<ExecutionBand>>;
    requires retry_stream_c<RequestStream> and drainable_stream_c<RequestStream> and stream_c<ExecutionStream>;
    requires std::same_as<source_value_t<RequestStream>, Record> and std::same_as<source_value_t<ExecutionStream>, Record>;
    requires std::constructible_from<DispatchSink, std::span<Target *>, std::span<Target>>;
    requires sink_c<ExecuteSink> and sink_c<CancelSink> and (not rejecting_sink_c<ExecuteSink>) and (not rejecting_sink_c<CancelSink>);
    requires std::same_as<typename ExecuteSink::value_t, Record> and std::same_as<typename CancelSink::value_t, Record>;
    requires std::default_initializable<ExecuteSink> and std::default_initializable<CancelSink>;
    requires std::move_constructible<ExecuteSink> and std::move_constructible<CancelSink>;
    requires request_dispatcher_c<Dispatcher> and std::constructible_from<Dispatcher, RequestStream &, DispatchSink>;
    requires work_executor_c<Executor> and std::constructible_from<Executor, ExecutionStream &, ExecuteSink>;
    requires work_executor_c<DispatchingExecutor> and std::move_constructible<DispatchingExecutor>
        and std::constructible_from<DispatchingExecutor, std::span<Dispatcher>, Executor &>;
};

} // namespace lcf::async::details

LCF_ASYNC_EXPORT
namespace lcf::async {

template <typename Components>
concept context_components_c = requires {
    typename Components::event_pump_t;
    typename Components::thread_handle_t;
    requires notify::event_pump_c<typename Components::event_pump_t>;
    requires std::default_initializable<typename Components::event_pump_t>;
    requires join_handle_c<typename Components::thread_handle_t>;
    requires std::default_initializable<typename Components::thread_handle_t>;
    requires std::assignable_from<typename Components::thread_handle_t &, typename Components::thread_handle_t>;
    typename Components::executable_t;
    typename Components::record_t;
    requires callback_executable_c<typename Components::executable_t>;
    requires scheduling_record_c<typename Components::record_t>;
    requires std::same_as<typename Components::executable_t, typename Components::record_t::executable_t>;
    requires std::constructible_from<typename Components::record_t, typename Components::executable_t &>;
    requires details::context_component_assembly_c<Components,
        notify::SignalNotifier<notify::EventPumpSignal<typename Components::event_pump_t>>>;
};

} // namespace lcf::async
