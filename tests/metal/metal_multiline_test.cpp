// metal_multiline_test.cpp — verbose GPU code spreads a call's arguments across many lines. This
// system test drives the generated mtl.* surface (cheatah::gpu::metal) with multi-line arguments —
// the Metal counterpart to systests/vulkan/test_multiline.purr — and runs on the software emulator
// (so it is checkable here) or on a real Apple GPU. Leak-clean: everything owned is released.
#include "gpu/metal/types.hpp"

#include <cstdint>
#include <cstdio>

namespace mtl = cheatah::gpu::metal;

#ifndef __APPLE__
#  include "gpu/metal/emulated/emulated.hpp"
namespace emu = cheatah::gpu::metal::emulated;
#endif

int main() {
    bool ok = true;
    std::printf("gpu.metal: multi-line arguments\n");
    {
        mtl::AutoreleasePool* pool = mtl::AutoreleasePool::alloc()->init();

        mtl::Device* dev = mtl::CreateSystemDefaultDevice();
        if (!dev) { std::printf("RESULT: FAIL (no Metal device)\n"); return 1; }
        mtl::CommandQueue* queue = dev->newCommandQueue();

        // newBuffer with its arguments on separate lines, closing `)` on the last argument.
        mtl::Buffer* a = dev->newBuffer(
            64 * sizeof(float),
            mtl::ResourceStorageModeShared);

        // and with the closing `)` on its own line.
        mtl::Buffer* b = dev->newBuffer(
            64 * sizeof(float),
            mtl::ResourceStorageModeShared
        );

        // Fill, encode a (trivial) command buffer with multi-line setBuffer calls, and read back.
        auto* ap = static_cast<float*>(a->contents());
        for (int i = 0; i < 64; ++i) ap[i] = float(i);

        mtl::CommandBuffer* cb = queue->commandBuffer();
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

        ok = (a->length() == 64 * sizeof(float)) && (b->length() == 64 * sizeof(float)) &&
             (static_cast<float*>(a->contents())[63] == 63.0f);
        std::printf("  device+queue+buffers via multi-line calls: %s\n", ok ? "ok" : "FAIL");

        a->release(); b->release(); queue->release(); dev->release();
        pool->release();
    }
#ifndef __APPLE__
    const unsigned long leaked = emu::live_objects();
    if (leaked != 0) { ok = false; std::printf("  LEAK: %lu objects still alive\n", leaked); }
#endif
    std::printf("RESULT: %s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
