#pragma once

#include "ThreadLauncher.h"

#include <concepts>
#include <thread>
#include <utility>

LCF_ASYNC_EXPORT
namespace lcf::async {

template <worker_driver_c Driver, join_handle_c ThreadHandle = std::thread>
class BasicWorker
{
public:
    ~BasicWorker() noexcept { this->join(); }
    BasicWorker() = delete;
    template <thread_launcher_c<ThreadHandle> Launcher, typename... DriverArgs>
    requires std::constructible_from<Driver, DriverArgs...>
    explicit BasicWorker(Launcher & launcher, DriverArgs &&... driver_args) :
        m_driver(std::forward<DriverArgs>(driver_args)...), m_thread_handle(launcher(&run_driver, this)) {}
    BasicWorker(const BasicWorker &) = delete;
    BasicWorker(BasicWorker &&) = delete;
    BasicWorker & operator=(const BasicWorker &) = delete;
    BasicWorker & operator=(BasicWorker &&) = delete;
public:
    void join() noexcept { if (m_thread_handle.joinable()) { m_thread_handle.join(); } }
    WorkerState getState() const noexcept { return m_driver.getState(); }
private:
    static void run_driver(void * state_p) noexcept
    {
        auto & worker = *static_cast<BasicWorker *>(state_p);
        worker.m_driver.run();
    }
private:
    Driver m_driver;
    ThreadHandle m_thread_handle;
};

} // namespace lcf::async
