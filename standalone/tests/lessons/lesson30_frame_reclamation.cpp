#include <NativeApi.h>
#include <gtest/gtest.h>
#include <cstdint>
#include <memory>
#include <optional>
#include <thread>

/*
 * Lesson 30: frame storage can be reused after its previous owner is gone.
 * Predict: repeated same-shaped tasks release their payloads every time even
 * if their frame address repeats. Address reuse is a cache observation, not
 * an ownership guarantee or a performance measurement.
 * Read the TEST, cached_work(), then frame.h's bins and provenance header.
 * Default blocks return to the destroying thread's cache; a custom-resource
 * block returns to its original resource (Lesson 29). A live default frame
 * can outlive its creator thread. A task held in TLS can even be destroyed
 * after that thread's cache retires: the retired path deletes directly.
 * Vary: same-thread reuse, cross-thread destruction and reverse TLS teardown.
 * Source map: details/frame.h, FrameCache::allocate/deallocate/is_retired;
 * verify/awaitable_test.cpp and existing frame benchmarks.
 */
namespace {
namespace ex = lcf::async::exec;
struct RecordAddress
{
    bool await_ready() const noexcept { return false; }
    bool await_suspend(std::coroutine_handle<> handle) const noexcept
    { m_address = reinterpret_cast<std::uintptr_t>(handle.address()); return false; }
    void await_resume() const noexcept {}
    std::uintptr_t & m_address;
};
#if defined(_MSC_VER)
__declspec(noinline)
#else
__attribute__((noinline))
#endif
lcf::async::Awaitable<int> cached_work(std::shared_ptr<int> marker, int & entries, std::uintptr_t & address)
{ ++entries; co_await RecordAddress {address}; co_return *marker; }
TEST(CoroutineLessons, Lesson30FrameReuseAndThreadLifetime)
{
    int entries = 0, repeated_addresses = 0;
    std::uintptr_t previous = 0;
    for (int index = 0; index < 8; ++index) {
        std::uintptr_t address = 0;
        auto marker = std::make_shared<int>(42);
        std::weak_ptr<int> observer = marker;
        auto result = ex::sync_wait(cached_work(std::move(marker), entries, address));
        ASSERT_TRUE(result);
        EXPECT_EQ(std::get<0>(*result), 42);
        EXPECT_TRUE(observer.expired());
        EXPECT_NE(address, 0u);
        repeated_addresses += address == previous;
        previous = address;
    }
    EXPECT_EQ(entries, 8);
    // Record the observation for --gtest_output=xml; cache reuse is not API.
    RecordProperty("repeated_frame_addresses", repeated_addresses);

    std::optional<lcf::async::Awaitable<int>> transferred;
    std::uintptr_t cross_thread_address = 0;
    auto marker = std::make_shared<int>(42);
    std::weak_ptr<int> observer = marker;
    std::jthread creator([&, marker = std::move(marker)]() mutable {
        transferred.emplace(cached_work(std::move(marker), entries, cross_thread_address));
    });
    creator.join(); // Its cache is retired, but the unstarted frame remains owned.
    EXPECT_EQ(entries, 8);
    EXPECT_FALSE(observer.expired());
    auto result = ex::sync_wait(std::move(*transferred));
    ASSERT_TRUE(result);
    EXPECT_EQ(std::get<0>(*result), 42);
    EXPECT_EQ(entries, 9);
    EXPECT_TRUE(observer.expired());

    struct LateHolder { LateHolder() noexcept {} std::optional<lcf::async::Awaitable<int>> m_task; };
    auto late_marker = std::make_shared<int>(42);
    std::weak_ptr<int> late_observer = late_marker;
    std::jthread retiring([&, marker = std::move(late_marker)]() mutable {
        thread_local LateHolder holder; // Constructed before the thread's first frame cache.
        holder.m_task.emplace(cached_work(std::move(marker), entries, cross_thread_address));
    });
    retiring.join(); // Cache destructor precedes holder destructor, in reverse construction order.
    EXPECT_EQ(entries, 9);
    EXPECT_TRUE(late_observer.expired());
}
}
