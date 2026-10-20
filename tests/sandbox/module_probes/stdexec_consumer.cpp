#include <coroutine>
#include <tuple>

#if PROBE_USE_MODULES
import probe.stdexec;
#else
#include <exec/task.hpp>
#include <exec/any_sender_of.hpp>
#include <stdexec/execution.hpp>
namespace probe {
using stdexec::just;
using stdexec::then;
using stdexec::sync_wait;
template <typename T = void>
using Task = exec::task<T>;
}
#endif

probe::Task<int> read_value() { co_return 42; }

int main()
{
    auto result = probe::sync_wait(probe::just(21) | probe::then([](int value) { return value * 2; }));
    auto coroutine = probe::sync_wait(read_value());
    return std::get<0>(*result) != 42 or std::get<0>(*coroutine) != 42;
}
