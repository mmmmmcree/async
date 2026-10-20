#include <NativeApi.h>
#include <atomic>
#include <iostream>
#include <thread>

int main()
{
    namespace async = lcf::async;
    namespace ex = async::exec;
    async::Context control;
    async::RunLoop first_worker;
    async::RunLoop second_worker;
    std::jthread first_thread {[&] { first_worker.run(); }};
    std::jthread second_thread {[&] { second_worker.run(); }};
    auto work = ex::when_all(
        ex::on(first_worker.getScheduler(), ex::just(20) | ex::then([](int value) { return value + 1; })),
        ex::on(second_worker.getScheduler(), ex::just(21)))
        | ex::continues_on(async::Scheduler {control.get_executor()})
        | ex::then([](int first, int second) { return first + second; });
    std::atomic_bool done = false;
    int result = 0;
    std::exception_ptr error;
    std::jthread waiter {[&] {
        try { if (auto values = ex::sync_wait(std::move(work))) { result = std::get<0>(*values); } }
        catch (...) { error = std::current_exception(); }
        done.store(true, std::memory_order_release);
    }};
    while (not done.load(std::memory_order_acquire)) {
        control.restart();
        if (control.poll_one() == 0) { std::this_thread::yield(); }
    }
    waiter.join();
    first_worker.finish(); second_worker.finish();
    if (error) { return 1; }
    std::cout << "Asio sender when_all result = " << result << '\n';
    return result == 42 ? 0 : 1;
}
