#pragma once

#include <NativeApi.h>
#include <chrono>
#include <thread>
#include <stdexcept>

namespace lcf::async::testing {

class Context
{
public:
    ~Context() noexcept { m_context.get_executor().on_work_finished(); }
    Context() { m_context.get_executor().on_work_started(); }
    auto get_executor() noexcept { return m_context.get_executor(); }
    Scheduler getScheduler() noexcept { return Scheduler {m_context.get_executor()}; }
    std::size_t poll_one() { m_context.restart(); return m_context.poll_one(); }
    std::size_t poll() { m_context.restart(); return m_context.poll(); }
    void runForever() { m_context.run(); }
    void stop() noexcept { m_context.stop(); }
    void restart() noexcept { m_context.restart(); }
private:
    lcf::async::Context m_context;
};

template <typename Function>
void post(Context & context, Function function) { lcf::async::post(context.get_executor(), std::move(function)); }

template <typename Predicate>
void pump_until(Context & context, Predicate predicate)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds {5};
    while (not predicate()) {
        if (not context.poll_one()) { std::this_thread::yield(); }
        if (std::chrono::steady_clock::now() >= deadline) { throw std::runtime_error("sender completion timed out"); }
    }
}

class Worker
{
public:
    ~Worker() noexcept { m_context.stop(); }
    Worker() : m_thread([this] { m_context.runForever(); }) {}
public:
    auto getScheduler() noexcept { return m_context.getScheduler(); }
    auto getExecutor() noexcept { return m_context.get_executor(); }
private:
    Context m_context;
    std::jthread m_thread;
};

}
