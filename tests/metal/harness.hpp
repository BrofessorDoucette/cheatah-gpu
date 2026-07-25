// harness.hpp — the shared GoogleTest fixture for the Metal suites (the mtl: ctests). The SAME
// fixture runs on real Apple hardware and on the software emulator (gpu/metal/emulated), exactly as
// the suites it serves: on Apple the objects are the system framework's; off Apple they are the
// emulator's, and TearDown asserts none of them leaked.
//
// Drive Metal through the cheatah-facing facade (cheatah::gpu::metal, the `mtl.*` surface) — the
// exact parallel to the Vulkan suite's vk:: — so the generated surface is exercised end-to-end.
#pragma once

#include "gpu/metal/types.hpp"

#include <gtest/gtest.h>

#ifndef __APPLE__
#  include "gpu/metal/emulated/emulated.hpp"
#endif

namespace cheatah::gpu::mtltest {

namespace mtl = cheatah::gpu::metal;

// Base fixture: autorelease pool -> device -> queue in SetUp, released in reverse in TearDown.
// Off Apple, TearDown then asserts the emulator's live-object count is back to zero (the leak
// counter is compiled in via CHEATAH_GPU_METAL_LEAKCHECK, see tests/CMakeLists.txt). On real Apple
// there is no emulator and no counter to ask — same guard as commit bedd041.
class MetalTest : public ::testing::Test {
  protected:
    void SetUp() override {
        pool_ = mtl::AutoreleasePool::alloc()->init();
        device_ = mtl::CreateSystemDefaultDevice();
        ASSERT_NE(device_, nullptr) << "no Metal device";
        queue_ = device_->newCommandQueue();
        ASSERT_NE(queue_, nullptr);
    }

    void TearDown() override {
        if (queue_ != nullptr) queue_->release();
        if (device_ != nullptr) device_->release();
        if (pool_ != nullptr) pool_->release();
#ifndef __APPLE__
        EXPECT_EQ(cheatah::gpu::metal::emulated::live_objects(), 0ul)
            << "emulator objects leaked by this test";
#endif
    }

    mtl::AutoreleasePool* pool_ = nullptr;
    mtl::Device* device_ = nullptr;
    mtl::CommandQueue* queue_ = nullptr;
};

}  // namespace cheatah::gpu::mtltest
