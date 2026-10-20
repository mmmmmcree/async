#pragma once

#include <NativeApi.h>
#include <Runtime.h>
#include <algorithm>
#include <array>
#include <stdexcept>

namespace lcf::async::testing::native {

namespace ex = lcf::async::exec;
namespace stde = lcf::async::exec;
using Context = lcf::async::testing::Context;

enum class Workload { eInline, eTransfers, eJoin };

struct Samples
{
    std::array<double, 8> m_poll_us {};
    unsigned m_callbacks = 0;
    unsigned m_max_steps_per_poll = 0;
};

struct Receiver
{
    using receiver_concept = stde::receiver_t;
    void set_value(int value) noexcept { m_value = value; m_done = true; }
    void set_error(std::exception_ptr error) noexcept { m_error = std::move(error); m_done = true; }
    void set_stopped() noexcept { m_done = true; }
    int & m_value;
    bool & m_done;
    std::exception_ptr & m_error;
};

template <typename Sender>
void run_pipeline(Context & context, Sender sender, unsigned & steps, Samples * samples_p)
{
    bool done = false;
    int value = 0;
    std::exception_ptr error;
    auto operation = stde::connect(std::move(sender), Receiver {value, done, error});
    stde::start(operation);
    while (not done) {
        const auto before_steps = steps;
        const auto before = samples_p ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point {};
        if (context.poll_one() == 0) { throw std::runtime_error("native workload lost its queued continuation"); }
        if (samples_p) {
            const auto elapsed = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - before).count();
            samples_p->m_poll_us.at(samples_p->m_callbacks++) = elapsed;
            samples_p->m_max_steps_per_poll = std::max(samples_p->m_max_steps_per_poll, steps - before_steps);
        }
    }
    if (error) { std::rethrow_exception(error); }
    if (value != 3 or steps != 3) { throw std::runtime_error("incorrect native workload result"); }
}

inline void run(Workload workload, Samples * samples_p = nullptr)
{
    Context context;
    const auto scheduler = context.getScheduler();
    unsigned steps = 0;
    const auto increment = [&](int value) { ++steps; return value + 1; };
    if (workload == Workload::eInline) {
        run_pipeline(context, ex::starts_on(scheduler,
            ex::just(0) | ex::then(increment) | ex::then(increment) | ex::then(increment)), steps, samples_p);
    } else if (workload == Workload::eTransfers) {
        run_pipeline(context, ex::just(0)
            | ex::continues_on(scheduler) | ex::then(increment)
            | ex::continues_on(scheduler) | ex::then(increment)
            | ex::continues_on(scheduler) | ex::then(increment), steps, samples_p);
    } else {
        run_pipeline(context, ex::when_all(
            ex::starts_on(scheduler, ex::just(0) | ex::then(increment)),
            ex::starts_on(scheduler, ex::just(0) | ex::then(increment)),
            ex::starts_on(scheduler, ex::just(0) | ex::then(increment)))
            | ex::then([](int first, int second, int third) { return first + second + third; }), steps, samples_p);
    }
}

}
