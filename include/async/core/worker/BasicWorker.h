#pragma once

#include <async/config/macros.h>
#include <async/core/error.h>
#include <async/core/scheduling/declarations/concepts.h>
#include <async/core/worker/declarations/concepts.h>
#include <async/core/worker/thread/ThreadLauncher.h>

#include <new>
#include <system_error>

LCF_ASYNC_EXPORT
namespace lcf::async {

template <work_executor_c Executor, worker_coordinator_c Coordinator, join_handle_c ThreadHandle>
class BasicWorker
{
    static void run_worker(void * p) noexcept { static_cast<BasicWorker *>(p)->run(); }
public:
    ~BasicWorker() noexcept { this->join(); }
    BasicWorker() = delete;
    BasicWorker(Executor executor, Coordinator & coordinator) : m_executor(std::move(executor)), m_coordinator_p(&coordinator) {}
    BasicWorker(const BasicWorker &) = delete;
    BasicWorker(BasicWorker &&) = delete;
    BasicWorker & operator=(const BasicWorker &) = delete;
    BasicWorker & operator=(BasicWorker &&) = delete;
public:
    template <thread_launcher_c<ThreadHandle> Launcher>
    std::error_code start(Launcher & launcher) noexcept
    {
        if (m_thread.joinable()) { return std::make_error_code(std::errc::operation_in_progress); }
        try { m_thread = launcher(&run_worker, this); }
        catch (const std::system_error & exception) { return exception.code(); }
        catch (const std::bad_alloc &) { return std::make_error_code(std::errc::not_enough_memory); }
        catch (...) { return errc::unexpected_exception; }
        return {};
    }
    void run() noexcept
    {
        while (true) {
            const auto observed = m_coordinator_p->observe();
            if (m_coordinator_p->isStopRequested()) { return; }
            const ExecutionReport report = m_executor.executeOne();
            if (report.isSucceeded()) { continue; }
            if (report.isFailed()) { m_coordinator_p->reportErrorCode(report.getErrorCode()); return; }
            if (report.isStopped() or m_coordinator_p->isFinished()) { return; }
            m_coordinator_p->wait(observed);
        }
    }
    void join() noexcept { if (m_thread.joinable()) { m_thread.join(); } }
private:
    Executor m_executor;
    Coordinator * m_coordinator_p;
    ThreadHandle m_thread;
};

} // namespace lcf::async
