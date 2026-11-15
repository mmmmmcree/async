#pragma once

#include <async/core/context/ContextInfo.h>
#include <async/core/context/DefaultContextComponents.h>
#include <async/core/context/details/ContextTracking.h>
#include <async/core/details/StorageLayout.h>
#include <async/core/details/containers/FixedCapacityVector.h>
#include <async/core/queue/utils/drain.h>

#include <array>

namespace lcf::async::details {

template <typename Components, typename Coordinator, typename Notifier>
class ContextResources
{
    using Record = typename Components::record_t;
    using SubmissionQueue = typename Components::submission_queue_t;
    using ExecutionQueue = typename Components::execution_queue_t;
    using Submission = typename Components::template submission_sink_t<SubmissionQueue, Notifier>;
    using ExecutionTarget = typename Components::template submission_sink_t<ExecutionQueue, Notifier>;
    using DispatchSink = typename Components::template dispatch_sink_t<ExecutionTarget>;
    using RequestBand = typename Components::template request_stream_t<SubmissionQueue>;
    using RequestStream = typename Components::template priority_stream_t<RequestBand>;
    using ExecutionBand = typename Components::template execution_stream_t<ExecutionQueue>;
    using ExecutionStream = typename Components::template priority_stream_t<ExecutionBand>;
    using Execute = typename Components::template execute_sink_t<Record>;
    using Cancel = typename Components::template cancel_sink_t<Record>;
    using Completion = CompletionTracker<Execute, Coordinator>;
    using Executor = typename Components::template executor_t<ExecutionStream, Completion>;
    using Dispatcher = typename Components::template dispatcher_t<RequestStream, DispatchSink>;

public:
    using admission_t = SubmissionGate<Submission, Coordinator>;

private:
    class SubmissionEndpoint
    {
    public:
        ~SubmissionEndpoint() noexcept = default;
        SubmissionEndpoint(Coordinator & coordinator, Notifier notifier) :
            m_sink(notifier), m_admission(m_sink, coordinator) {}
        SubmissionEndpoint(const SubmissionEndpoint &) = delete;
        SubmissionEndpoint(SubmissionEndpoint &&) = delete;
        SubmissionEndpoint & operator=(const SubmissionEndpoint &) = delete;
        SubmissionEndpoint & operator=(SubmissionEndpoint &&) = delete;

        SubmissionQueue & getQueue(std::size_t band) noexcept { return m_sink.getQueue(band); }
        admission_t & getAdmission() noexcept { return m_admission; }

    private:
        Submission m_sink;
        admission_t m_admission;
    };

    class ExecutionComponent
    {
    public:
        ~ExecutionComponent() noexcept = default;
        ExecutionComponent(std::span<ExecutionQueue *> queues, std::size_t count,
            std::size_t local, Coordinator & coordinator) :
            m_bands {ExecutionBand {queues.subspan(0, count), local}, ExecutionBand {queues.subspan(count, count), local},
                ExecutionBand {queues.subspan(2 * count, count), local}},
            m_stream(m_bands), m_executor(m_stream, Completion {Execute {}, coordinator}) {}
        ExecutionComponent(const ExecutionComponent &) = delete;
        ExecutionComponent(ExecutionComponent &&) = delete;
        ExecutionComponent & operator=(const ExecutionComponent &) = delete;
        ExecutionComponent & operator=(ExecutionComponent &&) = delete;

        ExecutionReport executeOne() noexcept { return m_executor.executeOne(); }

    private:
        std::array<ExecutionBand, k_priority_count> m_bands;
        ExecutionStream m_stream;
        Executor m_executor;
    };

    class DispatchComponent
    {
    public:
        ~DispatchComponent() noexcept = default;
        DispatchComponent(std::span<SubmissionQueue *> inputs, std::size_t count,
            std::span<ExecutionTarget *> targets, std::span<ExecutionTarget> shared_targets) :
            m_bands {RequestBand {inputs.subspan(0, count)}, RequestBand {inputs.subspan(count, count)},
                RequestBand {inputs.subspan(2 * count, count)}},
            m_stream(m_bands), m_dispatcher(m_stream, DispatchSink {targets, shared_targets}) {}
        DispatchComponent(const DispatchComponent &) = delete;
        DispatchComponent(DispatchComponent &&) = delete;
        DispatchComponent & operator=(const DispatchComponent &) = delete;
        DispatchComponent & operator=(DispatchComponent &&) = delete;

        DispatchReport dispatchOne() noexcept { return m_dispatcher.dispatchOne(); }
        void cancel(Coordinator & coordinator) noexcept
        {
            CompletionTracker<Cancel, Coordinator> sink {Cancel {}, coordinator};
            std::ranges::for_each(m_stream.drain(), [&sink](Record & record) noexcept { sink.receive(record); });
        }

    private:
        std::array<RequestBand, k_priority_count> m_bands;
        RequestStream m_stream;
        Dispatcher m_dispatcher;
    };

public:
    using execution_t = ExecutionComponent;
    using dispatcher_t = DispatchComponent;
    using record_t = Record;

    ~ContextResources() noexcept = default;
    ContextResources() noexcept = default;
    ContextResources(const ContextResources &) = delete;
    ContextResources(ContextResources &&) = delete;
    ContextResources & operator=(const ContextResources &) = delete;
    ContextResources & operator=(ContextResources &&) = delete;

    template <typename Storage>
    void prepareStorage(const ContextInfo & info, Storage & storage)
    {
        const std::size_t ports = info.getPortPriorities().size();
        const std::size_t shared = std::max<std::size_t>(info.getWorkerCount(), 1);
        storage.reserve(m_submissions, ports + 1);
        storage.reserve(m_local_targets, ports);
        storage.reserve(m_shared_targets, shared);
        storage.reserve(m_request_inputs, (ports + info.getDispatcherCount()) * k_priority_count);
        storage.reserve(m_execution_inputs, (ports + shared) * k_priority_count);
        storage.reserve(m_scheduler_targets, info.getTargets().size());
        storage.reserve(m_dispatchers, info.getDispatcherCount());
        storage.reserve(m_executors, shared + 1 + ports);
    }

    void create(const ContextInfo & info, Coordinator & coordinator, Notifier notifier)
    {
        StorageAllocator storage {info.getRuntimeMemoryResource()};
        this->prepareStorage(info, storage);
        const std::size_t ports = info.getPortPriorities().size();
        const std::size_t shared = std::max<std::size_t>(info.getWorkerCount(), 1);
        for (std::size_t i = 0; i <= ports; ++i) { m_submissions.emplace_back(coordinator, notifier); }
        for (std::size_t i = 0; i < ports; ++i) { m_local_targets.emplace_back(notifier); }
        for (std::size_t i = 0; i < shared; ++i) { m_shared_targets.emplace_back(notifier); }
        for (const auto & [is_shared, port] : info.getTargets()) {
            m_scheduler_targets.emplace_back(is_shared ? nullptr : &m_local_targets[port]);
        }
        for (std::size_t band = 0; band < k_priority_count; ++band) {
            for (ExecutionTarget & target : m_shared_targets) { m_execution_inputs.emplace_back(&target.getQueue(band)); }
        }
        for (std::size_t i = 0; i <= shared; ++i) {
            m_executors.emplace_back(std::span {m_execution_inputs}, shared, i % shared, coordinator);
        }
        for (ExecutionTarget & target : m_local_targets) {
            const std::size_t first = m_execution_inputs.size();
            for (std::size_t band = 0; band < k_priority_count; ++band) { m_execution_inputs.emplace_back(&target.getQueue(band)); }
            m_executors.emplace_back(std::span {m_execution_inputs}.subspan(first, k_priority_count), 1, 0, coordinator);
        }
        for (std::size_t dispatcher = 0; dispatcher < info.getDispatcherCount(); ++dispatcher) {
            const std::size_t first = m_request_inputs.size();
            std::size_t count = 0;
            for (std::size_t band = 0; band < k_priority_count; ++band) {
                count = 1;
                m_request_inputs.emplace_back(&m_submissions[0].getQueue(band));
                for (std::size_t port = dispatcher; port < ports; port += info.getDispatcherCount()) {
                    m_request_inputs.emplace_back(&m_submissions[port + 1].getQueue(band));
                    ++count;
                }
            }
            m_dispatchers.emplace_back(std::span {m_request_inputs}.subspan(first, count * k_priority_count), count,
                std::span {m_scheduler_targets}, std::span {m_shared_targets});
        }
    }

    admission_t & getSubmissionSink(std::size_t index) noexcept { return m_submissions[index].getAdmission(); }
    std::span<dispatcher_t> getDispatchers() noexcept { return m_dispatchers; }
    execution_t & getSharedExecutor(std::size_t index) noexcept { return m_executors[index]; }
    execution_t & getLocalExecutor(std::size_t shared_count, std::size_t port) noexcept { return m_executors[shared_count + port]; }

    void cancelRemaining(Coordinator & coordinator) noexcept
    {
        for (DispatchComponent & dispatcher : m_dispatchers) { dispatcher.cancel(coordinator); }
        CompletionTracker<Cancel, Coordinator> sink {Cancel {}, coordinator};
        const auto cancel_target = [&sink](ExecutionTarget & target) {
            for (std::size_t band = 0; band < k_priority_count; ++band) {
                std::ranges::for_each(queue::utils::drain(target.getQueue(band)),
                    [&sink](Record & record) noexcept { sink.receive(record); });
            }
        };
        for (ExecutionTarget & target : m_shared_targets) { cancel_target(target); }
        for (ExecutionTarget & target : m_local_targets) { cancel_target(target); }
    }

private:
    FixedCapacityVector<SubmissionEndpoint> m_submissions;
    FixedCapacityVector<ExecutionTarget> m_local_targets;
    FixedCapacityVector<ExecutionTarget> m_shared_targets;
    FixedCapacityVector<SubmissionQueue *> m_request_inputs;
    FixedCapacityVector<ExecutionQueue *> m_execution_inputs;
    FixedCapacityVector<ExecutionTarget *> m_scheduler_targets;
    FixedCapacityVector<DispatchComponent> m_dispatchers;
    FixedCapacityVector<ExecutionComponent> m_executors;
};

} // namespace lcf::async::details
