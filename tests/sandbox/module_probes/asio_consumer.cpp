#include <coroutine>

#if PROBE_USE_MODULES
import probe.asio;
#else
#include <asio.hpp>
namespace probe {
using Context = asio::io_context;
template <typename T = void>
using Awaitable = asio::awaitable<T>;
}
#endif

probe::Awaitable<int> read_value() { co_return 42; }

int main()
{
    probe::Context context;
    auto work = read_value();
    return static_cast<int>(context.poll());
}
