#pragma once

#include <async/config/macros.h>
#include <async/core/executable/AnyExecutableRef.h>
#include <async/core/scheduling/SchedulingRecord.h>
#include <async/core/queue/intrusive/mpmc/Queue.h>
#include <async/core/dispatcher/DispatchRequestSink.h>
#include <async/core/stream/DispatchRequestStream.h>
#include <async/core/stream/WorkStealingStream.h>
#include <async/core/stream/PriorityStream.h>
#include <async/core/sink/SubmissionSink.h>
#include <async/core/sink/ExecuteExecutableSink.h>
#include <async/core/sink/CancelExecutableSink.h>
#include <async/core/dispatcher/BasicDispatcher.h>
#include <async/core/executor/BasicExecutor.h>
#include <async/core/executor/DispatchingExecutor.h>
#include <async/core/policy/RoundRobin.h>
#include <async/core/policy/WeightedPriority.h>

LCF_ASYNC_EXPORT
namespace lcf::async {

struct DefaultContextComponents
{
    using executable_t = AnyExecutableRef;
    using record_t = SchedulingRecord<executable_t>;
    using submission_queue_t = queue::intrusive::mpmc::Queue<record_t>;
    using execution_queue_t = queue::intrusive::mpmc::Queue<record_t>;
    template <typename Dispatcher, typename Executor>
    using dispatching_executor_t = DispatchingExecutor<Dispatcher, Executor, policy::RoundRobin>;
    template <typename Queue, typename Notifier>
    using submission_sink_t = SubmissionSink<Queue, Notifier>;
    template <typename Target>
    using dispatch_sink_t = DispatchRequestSink<Target, policy::RoundRobin>;
    template <typename Record>
    using execute_sink_t = ExecuteExecutableSink<Record>;
    template <typename Record>
    using cancel_sink_t = CancelExecutableSink<Record>;
    template <typename Queue>
    using request_stream_t = DispatchRequestStream<Queue, policy::RoundRobin, policy::RoundRobin>;
    template <typename Queue>
    using execution_stream_t = WorkStealingStream<Queue, policy::RoundRobin>;
    template <typename Stream>
    using priority_stream_t = PriorityStream<Stream, policy::WeightedPriority>;
    template <typename Stream, typename Sink>
    using dispatcher_t = BasicDispatcher<Stream, Sink>;
    template <typename Stream, typename Sink>
    using executor_t = BasicExecutor<Stream, Sink>;
};

} // namespace lcf::async
