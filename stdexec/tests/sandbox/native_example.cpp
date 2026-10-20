#include <NativeApi.h>
#include <atomic>
#include <iostream>
#include <thread>

int main()
{
    namespace async = lcf::async;
    namespace ex = async::exec;
    async::Context control;
    async::Context first_worker;
    async::Context second_worker;
    std::jthread first_thread {[&] { first_worker.run(); }};
    std::jthread second_thread {[&] { second_worker.run(); }};
    auto work = ex::when_all(
        ex::on(first_worker.getScheduler(), ex::just(20) | ex::then([](int value) { return value + 1; })),
        ex::on(second_worker.getScheduler(), ex::just(21)))
        | ex::continues_on(control.getScheduler())
        | ex::then([](int first, int second) { return first + second; });
    std::atomic_bool done = false;
    int result = 0;
    std::exception_ptr error;
    std::jthread waiter {[&] {
        try {
            const auto values = ex::sync_wait(std::move(work));
            if (values) { result = std::get<0>(*values); }
        } catch (...) { error = std::current_exception(); }
        done.store(true, std::memory_order_release);
    }};
    while (not done.load(std::memory_order_acquire)) {
        const auto report = control.poll({.m_max_callbacks = 4, .m_max_duration = std::chrono::microseconds {200}});
        if (report.m_callbacks == 0) { std::this_thread::yield(); }
    }
    waiter.join();
    first_worker.stop();
    second_worker.stop();
    if (error) {
        try { std::rethrow_exception(error); }
        catch (const std::exception & exception) { std::cerr << exception.what() << '\n'; }
        return 1;
    }
    std::cout << "native when_all result = " << result << '\n';
    return result == 42 ? 0 : 1;
}
