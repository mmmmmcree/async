#include <async/core/scheduling/details/queue.h>
#include <async/core/scheduling/details/CallbackExecutor.h>
#include <async/core/timer/details/timer.h>
#include <async/core/synchronization/details/notification_state.h>
#include <async/core/coroutine/FrameAllocatorScope.h>
#include <async/core/coroutine/details/ReturnStorage.h>
#include <cstdint>
#include <async/core/cancellation/details/StopRegistrationGate.h>
#include <gtest/gtest.h>
#include <array>
#include <thread>

namespace {

namespace details = lcf::async::details;

/*
 * Objective: queue/callback mechanisms compile and function without stdexec include paths.
 * Input: two callback targets referring to one queue and a queued record.
 * Procedure: erase the targets, enqueue, inspect FIFO identity and stop/restart.
 * Expected: equal targets, one ready record and preserved queue contents across stop.
 * Failure hints: inspect queue.h and callback_executor.h; this target does not link async_stdexec.
 */
void queue_and_erasure_are_independent();
TEST(Mechanisms, QueueAndErasureAreIndependent) { queue_and_erasure_are_independent(); }

/*
 * Objective: frame allocation and registration gates need only the standard library.
 * Input: a selected PMR resource, one frame and a pre-cancelled registration gate.
 * Procedure: allocate under the resource, free after its scope, finish registration.
 * Expected: correctly aligned storage and one deferred cancellation decision.
 * Failure hints: inspect explicit frame includes, resource headers and StopRegistrationGate.
 */
void frame_and_gate_are_independent();
TEST(Mechanisms, FrameAndGateAreIndependent) { frame_and_gate_are_independent(); }

struct Target
{
    void enqueue(details::Callback * callback_p) const noexcept { m_queue_sp->push(callback_p); }
    void workStarted() const noexcept { m_queue_sp->workStarted(); }
    void workFinished() const noexcept { m_queue_sp->workFinished(); }
    bool operator==(const Target &) const noexcept = default;
    std::shared_ptr<details::Queue> m_queue_sp;
};
void queue_and_erasure_are_independent()
{
    Target target {std::make_shared<details::Queue>()};
    details::CallbackExecutor first {target};
    details::CallbackExecutor second {target};
    EXPECT_EQ(first, second);
    details::Callback callback {[](details::Callback *) {}};
    first.enqueue(&callback);
    EXPECT_EQ(target.m_queue_sp->readyCount(), 1u);
    target.m_queue_sp->stop();
    EXPECT_EQ(target.m_queue_sp->pop(false), nullptr);
    target.m_queue_sp->restart();
    EXPECT_EQ(target.m_queue_sp->pop(false), &callback);
    EXPECT_EQ(target.m_queue_sp->readyCount(), 0u);
}
void frame_and_gate_are_independent()
{
    std::pmr::synchronized_pool_resource resource;
    void * frame_p = nullptr;
    {
        lcf::async::FrameAllocatorScope scope {resource};
        frame_p = details::allocate_frame(128, 64);
    }
    EXPECT_EQ(reinterpret_cast<std::uintptr_t>(frame_p) % 64, 0u);
    details::deallocate_frame(frame_p, 128, 64);
    details::StopRegistrationGate gate;
    EXPECT_FALSE(gate.requestCancellation());
    EXPECT_TRUE(gate.finishRegistration());
}

}
