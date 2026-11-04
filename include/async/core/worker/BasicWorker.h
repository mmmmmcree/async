#pragma once

#include <async/core/worker/thread/ThreadLauncher.h>
#include <async/core/worker/declarations/concepts.h>

#include <cassert>
#include <concepts>
#include <type_traits>
#include <utility>

LCF_ASYNC_EXPORT
namespace lcf::async {

template <worker_driver_c Driver, join_handle_c ThreadHandle = NullThreadHandle>
class BasicWorker
{
public:
    ~BasicWorker() noexcept { this->join(); }
    BasicWorker() noexcept(std::is_nothrow_default_constructible_v<Driver>
        and std::is_nothrow_default_constructible_v<ThreadHandle>)
    requires std::default_initializable<Driver> and std::default_initializable<ThreadHandle> = default;
    template <typename... DriverArgs>
    requires (sizeof...(DriverArgs) > 0) and std::constructible_from<Driver, DriverArgs...>
        and std::default_initializable<ThreadHandle>
    explicit BasicWorker(DriverArgs &&... driver_args) noexcept(std::is_nothrow_constructible_v<Driver, DriverArgs...>
        and std::is_nothrow_default_constructible_v<ThreadHandle>) :
        m_driver(std::forward<DriverArgs>(driver_args)...) {}
    template <thread_launcher_c<ThreadHandle> Launcher, typename... DriverArgs>
    requires std::constructible_from<Driver, DriverArgs...>
    explicit BasicWorker(Launcher & launcher, DriverArgs &&... driver_args)
        noexcept(std::is_nothrow_constructible_v<Driver, DriverArgs...>
            and std::is_nothrow_invocable_r_v<ThreadHandle, Launcher &, ThreadEntry, void *>) :
        m_driver(std::forward<DriverArgs>(driver_args)...), m_thread_handle(launcher(&run_driver, this)) {}
    BasicWorker(const BasicWorker &) = delete;
    BasicWorker(BasicWorker &&) = delete;
    BasicWorker & operator=(const BasicWorker &) = delete;
    BasicWorker & operator=(BasicWorker &&) = delete;
public:
    template <typename... DriverArgs>
    requires requires(Driver & driver, DriverArgs &&... driver_args) {
        { driver.create(std::forward<DriverArgs>(driver_args)...) } -> std::same_as<void>;
    }
    void create(DriverArgs &&... driver_args) noexcept(noexcept(m_driver.create(std::forward<DriverArgs>(driver_args)...)))
    {
        assert(not m_thread_handle.joinable() and "Join the background Worker before reconfiguring its driver");
        m_driver.create(std::forward<DriverArgs>(driver_args)...);
    }
    void run() noexcept
    {
        assert(not m_thread_handle.joinable() and "Join the background Worker before driving it on the caller");
        m_driver.run();
    }
    bool executeOne() noexcept
    {
        assert(not m_thread_handle.joinable() and "Join the background Worker before driving it on the caller");
        return m_driver.executeOne();
    }
    void join() noexcept { if (m_thread_handle.joinable()) { m_thread_handle.join(); } }
    bool isJoinable() const noexcept { return m_thread_handle.joinable(); }
    WorkerState getState() const noexcept { return m_driver.getState(); }
private:
    static void run_driver(void * state_p) noexcept
    {
        auto & worker = *static_cast<BasicWorker *>(state_p);
        worker.m_driver.run();
    }
private:
    Driver m_driver;
    [[no_unique_address]] ThreadHandle m_thread_handle;
};

} // namespace lcf::async
