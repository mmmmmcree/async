# Standalone execution extensions

These standalone-only APIs live in `lcf::async::exec::ext`. Include
`<async/execution/extensions/expected.h>` (or `<async.h>`), or import `lcf.async`.

## Functions returning expected

`from_expected(function, arguments...)` creates a lazy sender.
`then_expected(sender, function)` and `sender | then_expected(function)` adapt a
pipeline step. Both require a callable whose invocation is `noexcept` and returns
`std::expected<T, std::error_code>` **by value**. `T` may be `void` or move-only.

```cpp
namespace ex = lcf::async::exec;
namespace ext = ex::ext;
using Result = std::expected<int, std::error_code>;

Result read_number() noexcept;        // Handles failures internally.
Result validate_number(int) noexcept;

auto work = ext::from_expected(read_number)
    | ext::then_expected(validate_number)
    | ex::upon_error([](std::error_code error) noexcept { return 0; });

auto result = ex::sync_wait(std::move(work)); // optional<tuple<int>>
```

The expected's state selects the completion channel:

| Result | Completion |
| --- | --- |
| Successful `expected<T, error_code>` | `set_value(T)` |
| Successful `expected<void, error_code>` | `set_value()` |
| Unexpected error code, including a zero code | `set_error(error_code)` |

Errors skip subsequent `then` and `then_expected` steps. `upon_error` supplies a
default value; `let_error` selects recovery work or propagates an error:

```cpp
auto work = ext::from_expected(read_number)
    | ex::let_error([](std::error_code & error) {
        return ext::from_expected([error]() noexcept -> Result {
            if (error == std::errc::no_such_file_or_directory) { return 0; }
            return std::unexpected(error);
        });
    });
```

`from_expected` stores the callable and decayed arguments. It invokes the callable
once on `start`, using the stored callable and arguments as rvalues. Use `std::ref`
for a borrowed argument and keep the referent alive until completion.
`then_expected` invokes its callable once on upstream `set_value`, forwarding the
upstream arguments. Upstream error and stopped completions bypass the callable.
Neither API creates a worker thread; use `starts_on` or a schedule sender when
execution needs to move to a scheduler.

These extensions add no allocation or `std::exception_ptr` completion. A source
has only value and `std::error_code` completions; a continuation also preserves
upstream error and stopped signatures. Sender construction and `connect` can still
throw while copying/moving user objects or connecting upstream work. Other
algorithms retain their own guarantees: standalone `then`, `upon_error` and
`let_error` can report `std::exception_ptr`, and `sync_wait` throws
`std::system_error` for an unhandled `std::error_code`. Recover errors before
waiting, or connect a receiver that handles error codes directly.

Use `exec::from_result(expected)` for an already computed result. Ordinary
`exec::then` continues to pass a returned expected as a value; explicitly use
`ext::then_expected` to unpack its state into completion channels.
