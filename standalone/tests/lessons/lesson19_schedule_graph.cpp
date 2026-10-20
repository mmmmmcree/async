#include <NativeApi.h>
#include <gtest/gtest.h>
#include <thread>

/*
 * Lesson 19: where does a graph start, complete, and obtain its environment?
 * Predict this table (home is the test thread, target a joined worker):
 *                 producer     consumer     producer's scheduler
 * starts_on       target       target       target
 * continues_on    home         target       home
 * on              target       home         target
 * Read the TEST, Work::Operation, Receiver. Unlike a coroutine continuation,
 * an ordinary then runs wherever its upstream completion is delivered.
 * Vary only the scheduling adaptor around the same immediate producer.
 * Source map: execution/scheduling.h, ScheduleState and OnSender::connect.
 */
namespace {
namespace ex = lcf::async::exec;
struct Trace { std::thread::id m_producer, m_consumer; lcf::async::Executor m_environment; int m_value = 0; };
struct Work
{
    using sender_concept = ex::sender_t;
    using completion_signatures = ex::completion_signatures<ex::set_value_t(int)>;
    template <class R> struct Operation
    {
        void start() noexcept
        {
            m_trace.m_producer = std::this_thread::get_id();
            m_trace.m_environment = lcf::async::Executor {ex::get_scheduler(ex::get_env(m_receiver))};
            ex::set_value(std::move(m_receiver), 21);
        }
        R m_receiver; Trace & m_trace;
    };
    template <class R> auto connect(R receiver) const { return Operation<R> {std::move(receiver), m_trace}; }
    Trace & m_trace;
};
struct Receiver
{
    using receiver_concept = ex::receiver_t;
    auto get_env() const noexcept { return ex::prop {ex::get_scheduler, m_home}; }
    void set_value(int value) noexcept { m_trace.m_consumer = std::this_thread::get_id(); m_trace.m_value = value; }
    void set_error(std::exception_ptr) noexcept { ADD_FAILURE() << "unexpected error"; }
    void set_stopped() noexcept { ADD_FAILURE() << "unexpected stop"; }
    lcf::async::Scheduler m_home; Trace & m_trace;
};
TEST(CoroutineLessons, Lesson19SchedulingAGraph)
{
    lcf::async::Context home, target;
    const auto caller = std::this_thread::get_id();
    Trace trace;
    auto observe = [&](auto graph, bool queued_producer, bool return_home) {
        auto operation = ex::connect(std::move(graph), Receiver {home.getScheduler(), trace});
        ex::start(operation);
        EXPECT_EQ(trace.m_producer == std::thread::id {}, queued_producer);
        EXPECT_EQ(trace.m_value, 0);
        std::thread::id worker_id;
        std::jthread worker([&] { worker_id = std::this_thread::get_id(); target.poll(); });
        worker.join();
        EXPECT_EQ(trace.m_producer, queued_producer ? worker_id : caller);
        if (return_home) { EXPECT_EQ(trace.m_value, 0); EXPECT_EQ(home.pollOne(), 1u); }
        EXPECT_EQ(trace.m_consumer, return_home ? caller : worker_id);
        EXPECT_EQ(trace.m_environment, queued_producer ? target.getExecutor() : home.getExecutor());
        EXPECT_EQ(trace.m_value, 21);
        trace = {};
    };
    observe(ex::starts_on(target.getScheduler(), Work {trace}), true, false);
    observe(ex::continues_on(Work {trace}, target.getScheduler()), false, false);
    observe(ex::on(target.getScheduler(), Work {trace}), true, true);
}
}
