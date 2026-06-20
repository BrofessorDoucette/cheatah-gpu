// Unit tests for gpu.dispatch — the compute-shader dispatch-dimensioning math. These exercise
// every branch of gpu/dispatch/dispatch.hpp so the QA gate's clang source-based coverage reports
// 100% lines + functions over the header.
#include <gtest/gtest.h>

#include "gpu/dispatch/dispatch.hpp"

namespace d = cheatah::gpu::dispatch;

TEST(Dispatch, CeilDiv) {
    EXPECT_EQ(d::ceil_div(0u, 64u), 0u);     // empty problem, denom != 0, remainder == 0
    EXPECT_EQ(d::ceil_div(64u, 64u), 1u);    // exact multiple (remainder == 0)
    EXPECT_EQ(d::ceil_div(65u, 64u), 2u);    // one over a boundary (remainder != 0)
    EXPECT_EQ(d::ceil_div(1000u, 256u), 4u); // 3 full groups + a partial
    EXPECT_EQ(d::ceil_div(10u, 0u), 0u);     // denom == 0 guard branch
    // No overflow near UINT32_MAX (the naive (n + d - 1) / d would wrap here).
    EXPECT_EQ(d::ceil_div(0xFFFFFFFFu, 1u), 0xFFFFFFFFu);
}

// constexpr smoke: also proves the functions are usable at compile time.
static_assert(d::ceil_div(65u, 64u) == 2u);
static_assert(d::ceil_div(10u, 0u) == 0u);

TEST(Dispatch, GroupCount1d) {
    EXPECT_EQ(d::group_count_1d(1000000u, 256u), 3907u);
    EXPECT_EQ(d::group_count_1d(0u, 256u), 0u);
    EXPECT_EQ(d::group_count_1d(256u, 256u), 1u);
}

TEST(Dispatch, ClampGroupCount) {
    EXPECT_EQ(d::clamp_group_count(10u, 65535u), 10u);       // want < device_max
    EXPECT_EQ(d::clamp_group_count(70000u, 65535u), 65535u); // want > device_max -> clamp
    EXPECT_EQ(d::clamp_group_count(65535u, 65535u), 65535u); // want == device_max (boundary)
}

static_assert(d::clamp_group_count(70000u, 65535u) == 65535u);
