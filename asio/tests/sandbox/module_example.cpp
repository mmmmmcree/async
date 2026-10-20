#include <chrono>
#include <coroutine>
#include <exception>
#include <iostream>
#include <stop_token>
#include <thread>
#include <tuple>
#include <utility>

import lcf.async;

namespace async = lcf::async;
namespace ex = async::exec;

namespace {
async::Awaitable<int> read_value(int value)
{
    co_await async::yield();
    co_return value;
}
}

int main()
{
    async::RunLoop worker;
    auto scheduler = worker.getScheduler();
    std::jthread worker_thread {[&](std::stop_token token) {
        std::stop_callback on_stop {token, [&] { worker.finish(); }};
        worker.run();
    }};
    try {
        auto shared = ex::split(ex::when_all(
            async::as_sender(scheduler, read_value(20)),
            async::as_sender(scheduler, read_value(22)))
            | ex::then([](int first, int second) { return first + second; }));
        const auto values = ex::sync_wait(ex::when_all(
            shared | ex::then([](const int & value) { return value + 1; }),
            shared | ex::then([](const int & value) { return value + 2; })));
        if (not values or std::get<0>(*values) != 43 or std::get<1>(*values) != 44) { return 1; }
        if (not ex::sync_wait(async::schedule_after(scheduler, std::chrono::milliseconds {0}))) { return 1; }
        async::Signal ready {scheduler.getExecutor()};
        ready.set();
        if (not ex::sync_wait(async::as_sender(scheduler, ready.wait()))) { return 1; }
        std::cout << "module shared results = " << std::get<0>(*values) << ", " << std::get<1>(*values) << '\n';
        return 0;
    } catch (const std::exception & error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
