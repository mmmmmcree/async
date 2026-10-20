#include <async.h>
#include <iostream>
#include <thread>

int main()
{
    namespace async = lcf::async;
    namespace ex = async::exec;
    async::Context context;
    std::jthread worker {[&](std::stop_token stop) {
        std::stop_callback shutdown {stop, [&] { context.stop(); }};
        context.run();
    }};
    auto shared = ex::split(ex::starts_on(context.getScheduler(), ex::just(20) | ex::then([](int value) { return value + 2; })));
    auto result = ex::sync_wait(ex::when_all(shared | ex::then([](const int & value) { return value + 1; }), shared | ex::then([](const int & value) { return value * 2; })));
    context.stop();
    if (not result) { return 1; }
    std::cout << std::get<0>(*result) << " " << std::get<1>(*result) << "\n";
    return std::get<0>(*result) == 23 and std::get<1>(*result) == 44 ? 0 : 1;
}
