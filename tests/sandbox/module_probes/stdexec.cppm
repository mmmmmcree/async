module;
#include <exec/task.hpp>
#include <exec/any_sender_of.hpp>
#include <stdexec/execution.hpp>

export module probe.stdexec;

export namespace probe {
using stdexec::just;
using stdexec::then;
using stdexec::sync_wait;
template <typename T = void>
using Task = exec::task<T>;
}
