#pragma once
#include <async/config/macros.h>
#include <async/core/details/ActivityGate.h>
#include <async/core/scheduling/declarations/concepts.h>
#include <async/core/worker/thread/ThreadLauncher.h>
LCF_ASYNC_EXPORT
namespace lcf::async {
template <work_executor_c ExecutionPath, typename Coordinator, join_handle_c ThreadHandle>
class BasicWorker
{
    static void runWorker(void * p) noexcept { static_cast<BasicWorker *>(p)->run(); }
public:
    ~BasicWorker() noexcept { this->join(); }
    BasicWorker() = delete;
    BasicWorker(ExecutionPath path, Coordinator & coordinator) : m_path(std::move(path)), m_coordinator_p(&coordinator) {}
    BasicWorker(const BasicWorker &) = delete;
    BasicWorker(BasicWorker &&) = delete;
    BasicWorker & operator=(const BasicWorker &) = delete;
    BasicWorker & operator=(BasicWorker &&) = delete;
    template <thread_launcher_c<ThreadHandle> Launcher>
    void start(Launcher & launcher) { m_thread = launcher(&runWorker, this); }
    void run() noexcept
    {
        while (true) {
            const auto observed = m_coordinator_p->observe();
            ExecutionReport report;
            {
                const details::ExecutionGuard guard {*m_coordinator_p};
                if (not guard) { return; }
                report = m_path.executeOne();
            }
            if (report.m_error_code) { m_coordinator_p->reportErrorCode(report.m_error_code); return; }
            if (report.isStopped()) { return; }
            if (report.hasProgress()) { continue; }
            if (m_coordinator_p->isFinished()) { return; }
            m_coordinator_p->wait(observed);
        }
    }
    void join() noexcept { if (m_thread.joinable()) { m_thread.join(); } }
private:
    ExecutionPath m_path;
    Coordinator * m_coordinator_p;
    ThreadHandle m_thread;
};
}
