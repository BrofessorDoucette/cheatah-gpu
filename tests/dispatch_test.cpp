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

TEST(Dispatch, Dim3Defaults) {
    constexpr d::Dim3 one_d{1000u};  // unused axes default to 1 (one slice, not zero)
    EXPECT_EQ(one_d.x, 1000u);
    EXPECT_EQ(one_d.y, 1u);
    EXPECT_EQ(one_d.z, 1u);
}

TEST(Dispatch, Dim3Equality) {
    EXPECT_TRUE(d::Dim3({8u, 8u, 1u}) == d::Dim3({8u, 8u, 1u}));
    EXPECT_FALSE(d::Dim3({8u, 8u, 1u}) == d::Dim3({9u, 8u, 1u}));  // x differs
    EXPECT_FALSE(d::Dim3({8u, 8u, 1u}) == d::Dim3({8u, 9u, 1u}));  // y differs
    EXPECT_FALSE(d::Dim3({8u, 8u, 1u}) == d::Dim3({8u, 8u, 2u}));  // z differs
}

TEST(Dispatch, GroupCount3d) {
    // exact multiple / one-over / partial, one behavior per axis in a single call.
    EXPECT_TRUE(d::group_count_3d({256u, 65u, 1000u}, {256u, 64u, 256u}) ==
                d::Dim3({1u, 2u, 4u}));
    // a zero local_size axis yields 0 on that axis (ceil_div's guard), others unaffected.
    EXPECT_TRUE(d::group_count_3d({100u, 100u, 100u}, {0u, 10u, 10u}) == d::Dim3({0u, 10u, 10u}));
    // overflow-safe near UINT32_MAX on every axis.
    EXPECT_TRUE(d::group_count_3d({0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu}, {1u, 1u, 1u}) ==
                d::Dim3({0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu}));
    // the dispatch must cover every item on every axis.
    constexpr d::Dim3 items{1000u, 30u, 7u};
    constexpr d::Dim3 local{256u, 8u, 4u};
    constexpr d::Dim3 groups = d::group_count_3d(items, local);
    EXPECT_GE(groups.x * local.x, items.x);
    EXPECT_GE(groups.y * local.y, items.y);
    EXPECT_GE(groups.z * local.z, items.z);
}

static_assert(d::group_count_3d({65u, 64u, 1u}, {64u, 64u, 64u}) == d::Dim3({2u, 1u, 1u}));

TEST(Dispatch, ClampGroupCount3d) {
    // per-axis: under / over / equal in one call.
    EXPECT_TRUE(d::clamp_group_count(d::Dim3({10u, 70000u, 65535u}),
                                     d::Dim3({65535u, 65535u, 65535u})) ==
                d::Dim3({10u, 65535u, 65535u}));
}

static_assert(d::clamp_group_count(d::Dim3({70000u, 1u, 1u}), d::Dim3({65535u, 65535u, 65535u})) ==
              d::Dim3({65535u, 1u, 1u}));
