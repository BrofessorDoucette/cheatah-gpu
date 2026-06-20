// Unit tests for gpu.backend — compile-time backend selection + the shared-interface conventions
// (static typing, concepts, the optional pattern). These cover every function/branch of
// gpu/backend.hpp so the QA gate reports 100% lines + functions over the header.
#include <gtest/gtest.h>

#include "gpu/backend.hpp"

namespace g = cheatah::gpu;

TEST(Backend, Name) {
    // both ternary arms
    EXPECT_EQ(g::backend_name(g::Backend::vulkan), "vulkan");
    EXPECT_EQ(g::backend_name(g::Backend::metal), "metal");
}
static_assert(g::backend_name(g::Backend::metal) == "metal");

TEST(Backend, Active) {
    // This build defines no CHEATAH_GPU_BACKEND_* and isn't Apple, so Vulkan is selected.
    EXPECT_EQ(g::active_backend, g::Backend::vulkan);
    EXPECT_EQ(g::active_backend_name(), "vulkan");
}

TEST(Backend, FromName) {
    EXPECT_EQ(g::backend_from_name("vulkan"), std::optional{g::Backend::vulkan}); // first if true
    EXPECT_EQ(g::backend_from_name("metal"), std::optional{g::Backend::metal});   // second if true
    EXPECT_FALSE(g::backend_from_name("d3d12").has_value());                      // neither -> nullopt
}

TEST(Backend, IsActive) {
    EXPECT_TRUE(g::is_active(g::active_backend));      // true branch
    EXPECT_FALSE(g::is_active(g::Backend::metal));     // false branch (Metal isn't active here)
}
static_assert(g::is_active(g::Backend::vulkan));
