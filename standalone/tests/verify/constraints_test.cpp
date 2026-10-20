#include <Observation.h>
#include <gtest/gtest.h>
#include <string>

namespace {
namespace ex = lcf::async::exec;

/*
 * Objective: invalid continuation signatures are rejected at the callable boundary.
 * Input: a value sender, mismatched callbacks and a non-sender let_value return.
 * Procedure: query invocability without instantiating an operation graph.
 * Expected: valid then is accepted and both invalid forms are rejected.
 * Failure hints: inspect continuation constraints before completion-signature transformation.
 */
void continuation_constraints();
TEST(Protocol, ContinuationConstraints) { continuation_constraints(); }

void continuation_constraints()
{
    using Source = decltype(ex::just(42));
    const auto valid = [](int) { return 0; };
    const auto mismatch = [](std::string) { return 0; };
    static_assert(std::invocable<decltype(ex::then), Source, decltype(valid)>);
    static_assert(not std::invocable<decltype(ex::then), Source, decltype(mismatch)>);
    static_assert(not std::invocable<decltype(ex::let_value), Source, decltype(valid)>);
}

}
