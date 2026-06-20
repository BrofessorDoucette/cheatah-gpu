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

TEST(Backend, Resolution) {
    // No CHEATAH_GPU_BACKEND_* is forced and this isn't Apple, so the requested backend is the active
    // one (Vulkan) and nothing was switched or flagged suboptimal.
    EXPECT_EQ(g::requested_backend, g::Backend::vulkan);
    EXPECT_FALSE(g::metal_available);
    EXPECT_FALSE(g::backend_was_switched);
    EXPECT_FALSE(g::backend_is_suboptimal);
}
static_assert(g::requested_backend == g::Backend::vulkan);
static_assert(!g::backend_was_switched && !g::backend_is_suboptimal);

TEST(Backend, WarnSwitched) {
    // The selection is optimal in this build, so the runtime notice is a no-op: nothing is printed and
    // the one-shot guard stays clear. (The switched/suboptimal message paths are exercised by the
    // wrong-backend system test, which compiles a forced backend on this platform.)
    EXPECT_FALSE(g::warn_backend_selection(stderr));
    EXPECT_FALSE(g::backend_warning_emitted());
}

TEST(Backend, Silence) {
    g::silence_backend_warning(true);              // mute (default arg path covered below)
    EXPECT_TRUE(g::backend_warning_silenced());
    EXPECT_FALSE(g::warn_backend_selection());
    g::silence_backend_warning(false);             // re-enable (false arm)
    EXPECT_FALSE(g::backend_warning_silenced());
    g::silence_backend_warning();                  // default argument = true
    EXPECT_TRUE(g::backend_warning_silenced());
    g::silence_backend_warning(false);             // leave clean for other tests
}
