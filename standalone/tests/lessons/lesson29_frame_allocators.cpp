#include <NativeApi.h>
#include <gtest/gtest.h>
#include <map>
#include <memory_resource>

/*
 * Lesson 29: frame, execution, timer heap and registration resources are distinct.
 * Predict: FrameAllocatorScope allocates before lazy body entry. with_allocator
 * later supplies an execution allocator; it cannot relocate that existing
 * frame. Destruction uses the resource recorded when the frame was created.
 * Context's resource controls its shared timer heap. delay(duration, resource)
 * controls the individual registration allocated by connect. start inserts it
 * into the heap; delivery releases the registration, but heap capacity stays
 * until the queue dies. One resource setting does not override the others.
 * Read the TEST, make_work(), InspectAllocator, CountingResource.
 * The resources outlive every frame/operation using them. Paired size/alignment
 * checks validate provenance, not just equal allocation/deallocation counts.
 * Vary: run a frame after its allocation scope ends, then discard an unstarted
 * frame. Noinline factories keep the allocation experiment visible in Release.
 * Source map: FrameAllocatorScope.h, promise operator new/delete, frame.h,
 * execution/environment.h, ReturnStorage.h, Context.h, timer/operations.h,
 * timer/details/timer.h and TimerService.h.
 */
namespace {
namespace ex = lcf::async::exec;
class CountingResource : public std::pmr::memory_resource
{
public:
    int m_allocations = 0, m_deallocations = 0;
    bool balanced() const { return m_blocks.empty() && m_allocations == m_deallocations; }
private:
    void * do_allocate(std::size_t bytes, std::size_t alignment) override
    {
        auto * block = std::pmr::new_delete_resource()->allocate(bytes, alignment);
        m_blocks.emplace(block, std::pair {bytes, alignment});
        ++m_allocations;
        return block;
    }
    void do_deallocate(void * block, std::size_t bytes, std::size_t alignment) override
    {
        auto found = m_blocks.find(block);
        EXPECT_NE(found, m_blocks.end());
        if (found != m_blocks.end()) {
            EXPECT_EQ(found->second, (std::pair {bytes, alignment}));
            m_blocks.erase(found);
        }
        ++m_deallocations;
        std::pmr::new_delete_resource()->deallocate(block, bytes, alignment);
    }
    bool do_is_equal(const std::pmr::memory_resource & other) const noexcept override { return this == &other; }
    std::map<void *, std::pair<std::size_t, std::size_t>> m_blocks;
};
struct InspectAllocator
{
    using sender_concept = ex::sender_t;
    using completion_signatures = ex::completion_signatures<ex::set_value_t(int), ex::set_error_t(std::exception_ptr)>;
    template <class R> struct Operation
    {
        void start() noexcept
        {
            try {
                const auto allocator = ex::get_allocator(ex::get_env(m_receiver));
                m_observed = allocator.resource();
                int result;
                { std::pmr::vector<int> values {allocator.resource()}; values.push_back(21); result = values.front() * 2; }
                ex::set_value(std::move(m_receiver), result);
            } catch (...) { ex::set_error(std::move(m_receiver), std::current_exception()); }
        }
        R m_receiver; std::pmr::memory_resource * & m_observed;
    };
    template <class R> auto connect(R receiver) const { return Operation<R> {std::move(receiver), m_observed}; }
    std::pmr::memory_resource * & m_observed;
};
#if defined(_MSC_VER)
__declspec(noinline)
#else
__attribute__((noinline))
#endif
lcf::async::Awaitable<int> make_work(int & entries, std::pmr::memory_resource * & observed, std::shared_ptr<int> marker)
{
    ++entries;
    const int result = co_await InspectAllocator {observed};
    (void)marker;
    co_return result;
}
struct TimerReceiver
{
    using receiver_concept = ex::receiver_t;
    auto get_env() const noexcept { return ex::prop {ex::get_scheduler, m_scheduler}; }
    void set_value() noexcept { ++m_completions; }
    void set_error(std::exception_ptr) noexcept { ADD_FAILURE() << "unexpected timer error"; }
    void set_stopped() noexcept { ADD_FAILURE() << "unexpected timer stop"; }
    lcf::async::Scheduler m_scheduler;
    int & m_completions;
};
TEST(CoroutineLessons, Lesson29FrameAndExecutionAllocators)
{
    CountingResource frames, execution;
    std::pmr::memory_resource * observed = nullptr;
    int entries = 0;
    auto marker = std::make_shared<int>(42);
    std::weak_ptr<int> observer = marker;
    auto work = [&] {
        const lcf::async::FrameAllocatorScope scope {frames};
        return make_work(entries, observed, std::move(marker));
    }();
    EXPECT_EQ(entries, 0);
    EXPECT_EQ(frames.m_allocations, 1);
    EXPECT_EQ(frames.m_deallocations, 0);
    EXPECT_EQ(execution.m_allocations, 0);
    auto result = ex::sync_wait(ex::with_allocator(std::move(work), &execution));
    ASSERT_TRUE(result);
    EXPECT_EQ(std::get<0>(*result), 42);
    EXPECT_EQ(entries, 1);
    EXPECT_EQ(observed, &execution);
    EXPECT_GT(execution.m_allocations, 0);
    EXPECT_TRUE(frames.balanced());
    EXPECT_TRUE(execution.balanced());
    EXPECT_TRUE(observer.expired());
    {
        const lcf::async::FrameAllocatorScope scope {frames};
        auto discarded = make_work(entries, observed, {});
        EXPECT_EQ(frames.m_allocations, 2);
    }
    EXPECT_EQ(entries, 1);
    EXPECT_TRUE(frames.balanced());

    // Revisit Lesson 27 with two independent timer resources.
    CountingResource timer_heap, registrations;
    {
        lcf::async::Context context {&timer_heap};
        const auto initial_heap_allocations = timer_heap.m_allocations; // Debug container proxies may already exist.
        int completions = 0;
        auto operation = ex::connect(lcf::async::delay(std::chrono::nanoseconds {0}, &registrations),
            TimerReceiver {context.getScheduler(), completions});
        EXPECT_GT(registrations.m_allocations, 0);
        EXPECT_EQ(timer_heap.m_allocations, initial_heap_allocations);
        const auto registration_allocations = registrations.m_allocations;

        ex::start(operation);
        EXPECT_GT(timer_heap.m_allocations, initial_heap_allocations);
        EXPECT_EQ(registrations.m_allocations, registration_allocations);
        EXPECT_EQ(completions, 0);
        EXPECT_EQ(context.pollOne(), 1u);
        EXPECT_EQ(completions, 1);
        EXPECT_TRUE(registrations.balanced());
        EXPECT_FALSE(timer_heap.balanced()); // An empty heap retains its buffer.
        context.shutdown();
    }
    EXPECT_TRUE(timer_heap.balanced());
    EXPECT_TRUE(registrations.balanced());
}
}
