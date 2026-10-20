#include <NativeApi.h>
#include <gtest/gtest.h>
#include <map>
#include <memory_resource>

/*
 * Lesson 29: frame creation and execution query different allocator policies.
 * Predict: FrameAllocatorScope allocates before lazy body entry. with_allocator
 * later supplies an execution allocator; it cannot relocate that existing
 * frame. Destruction uses the resource recorded when the frame was created.
 * Read the TEST, make_work(), InspectAllocator, CountingResource.
 * The resources outlive every frame/operation using them. Paired size/alignment
 * checks validate provenance, not just equal allocation/deallocation counts.
 * Vary: run a frame after its allocation scope ends, then discard an unstarted
 * frame. Noinline factories keep the allocation experiment visible in Release.
 * Source map: FrameAllocatorScope.h, promise operator new/delete, frame.h,
 * execution/environment.h, ReturnStorage.h.
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
}
}
