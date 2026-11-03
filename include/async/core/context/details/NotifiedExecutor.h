#pragma once

#include "WorkSignal.h"
#include <async/core/concepts.h>

namespace lcf::async::details {

template <work_executor_c Executor>
class NotifiedExecutor
{
public:
    ~NotifiedExecutor() noexcept = default;
    NotifiedExecutor() = delete;
    template <context_executor_factory_c<Executor> Factory>
    NotifiedExecutor(Factory & factory, WorkSignal & signal, WorkSignal & loop_signal) :
        m_executor(factory.makeExecutor()), m_signal_p(&signal), m_loop_signal_p(&loop_signal) {}
    NotifiedExecutor(const NotifiedExecutor &) = delete;
    NotifiedExecutor(NotifiedExecutor &&) = delete;
    NotifiedExecutor & operator=(const NotifiedExecutor &) = delete;
    NotifiedExecutor & operator=(NotifiedExecutor &&) = delete;
public:
    template <work_record_c Record>
        requires record_submitter_c<Executor, Record>
    EnqueueResult tryEnqueue(Record & record) noexcept
    {
        const auto result = m_executor.tryEnqueue(record);
        if (result == EnqueueResult::eAccepted) {
            m_signal_p->notifyOne();
            if (m_loop_signal_p != m_signal_p) { m_loop_signal_p->notifyOne(); }
        }
        return result;
    }
    bool executeOne() noexcept { return m_executor.executeOne(); }
    bool isEmpty() const noexcept { return m_executor.isEmpty(); }
    void close() noexcept { m_executor.close(); }
private:
    Executor m_executor;
    WorkSignal * m_signal_p;
    WorkSignal * m_loop_signal_p;
};

} // namespace lcf::async::details
