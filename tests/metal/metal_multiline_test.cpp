// metal_multiline_test.cpp — verbose GPU code spreads a call's arguments across many lines. This
// suite drives the generated mtl.* surface (cheatah::gpu::metal) with multi-line arguments — the
// Metal counterpart to systests/vulkan/test_multiline.purr — and runs on the software emulator (so
// it is checkable everywhere) or on a real Apple GPU. Leak-clean: everything owned is released, and
// the fixture's TearDown asserts it (emulator builds).
#include "harness.hpp"

#include <cstdint>

namespace cheatah::gpu::mtltest {
namespace {

class MetalMultiline : public MetalTest {};

TEST_F(MetalMultiline, DeviceQueueBuffersViaMultiLineCalls) {
    // newBuffer with its arguments on separate lines, closing `)` on the last argument.
    mtl::Buffer* a = device_->newBuffer(
        64 * sizeof(float),
        mtl::ResourceStorageModeShared);

    // and with the closing `)` on its own line.
    mtl::Buffer* b = device_->newBuffer(
        64 * sizeof(float),
        mtl::ResourceStorageModeShared
    );
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);

    // Fill, encode a (trivial) command buffer with multi-line setBuffer calls, and read back.
    auto* ap = static_cast<float*>(a->contents());
    for (int i = 0; i < 64; ++i) ap[i] = float(i);

    mtl::CommandBuffer* cb = queue_->commandBuffer();
    mtl::ComputeCommandEncoder* enc = cb->computeCommandEncoder();
    enc->setBuffer(
        a,
        0,
        0);
    enc->setBuffer(
        b,
        0,
        1);
    enc->endEncoding();
    cb->commit();
    cb->waitUntilCompleted();

    EXPECT_EQ(a->length(), 64 * sizeof(float));
    EXPECT_EQ(b->length(), 64 * sizeof(float));
    EXPECT_EQ(static_cast<float*>(a->contents())[63], 63.0f);

    a->release();
    b->release();
}

}  // namespace
}  // namespace cheatah::gpu::mtltest
