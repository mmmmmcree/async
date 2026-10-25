#if LCF_TEST_USE_MODULES
import lcf.async;
#else
#include <async/execution.h>
#endif

#include <iostream>
#include <tuple>
#include <utility>

int main()
{
    namespace ex = lcf::async::exec;

    // Composition is lazy: the body runs when sync_wait starts the operation.
    auto work = ex::just(21)
        | ex::then([](int value) noexcept { return value * 2; });
    auto result = lcf::async::this_thread::sync_wait(std::move(work));
    if (not result) { return 1; }

    const auto value = std::get<0>(*result);
    std::cout << "execution result: " << value << '\n';
    return value == 42 ? 0 : 1;
}
