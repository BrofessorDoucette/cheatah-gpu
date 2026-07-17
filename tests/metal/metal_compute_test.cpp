// metal_compute_test.cpp — Metal compute, the SAME test on real Apple hardware and on the software
// emulator. It carries real Metal Shading Language (MSL) source: on Apple, Metal compiles it and the
// kernels run on the GPU; off Apple, the software device (gpu/metal/emulated) ignores the MSL and runs
// the registered C++ stand-ins on the CPU instead. Either way the canonical flow is exercised — device
// -> library -> function -> pipeline -> queue -> command buffer -> compute encoder -> setBuffer ->
// dispatchThreads -> commit -> read contents() — and the results are checked bit-for-bit. Two kernels
// (add, multiply) give "a couple" of real-hardware checks. Leak-clean: everything owned is released.
// Drive Metal through the cheatah-facing facade (cheatah::gpu::metal, the `mtl.*` surface) — the exact
// parallel to the Vulkan systest using vk.* — so the generated surface is exercised end-to-end.
#include "gpu/metal/types.hpp"

#include <cstdint>
#include <cstdio>
#include <vector>

namespace mtl = cheatah::gpu::metal;

#ifndef __APPLE__
#  include "gpu/metal/emulated/emulated.hpp"
namespace emu = cheatah::gpu::metal::emulated;
#endif

// Real MSL — compiled by Metal on Apple; ignored by the emulator (which dispatches by function name).
static const char* kSource = R"MSL(
#include <metal_stdlib>
using namespace metal;
kernel void add_arrays(device const float* A [[buffer(0)]],
                       device const float* B [[buffer(1)]],
                       device float* C       [[buffer(2)]],
                       uint i [[thread_position_in_grid]]) { C[i] = A[i] + B[i]; }
kernel void mul_arrays(device const float* A [[buffer(0)]],
                       device const float* B [[buffer(1)]],
                       device float* C       [[buffer(2)]],
                       uint i [[thread_position_in_grid]]) { C[i] = A[i] * B[i]; }
kernel void iota3d(device float* C [[buffer(0)]],
                   uint3 p [[thread_position_in_grid]],
                   uint3 g [[threads_per_grid]]) {
    C[p.z * g.x * g.y + p.y * g.x + p.x] = float(p.z * g.x * g.y + p.y * g.x + p.x);
}
)MSL";

#ifndef __APPLE__
// CPU stand-ins for the emulator, matched to the kernels above by name.
static void add_arrays(void** b, unsigned n, unsigned long w) {
    if (n < 3) return;
    auto* A = static_cast<const float*>(b[0]); auto* B = static_cast<const float*>(b[1]); auto* C = static_cast<float*>(b[2]);
    for (std::uint64_t i = 0; i < w; ++i) C[i] = A[i] + B[i];
}
static void mul_arrays(void** b, unsigned n, unsigned long w) {
    if (n < 3) return;
    auto* A = static_cast<const float*>(b[0]); auto* B = static_cast<const float*>(b[1]); auto* C = static_cast<float*>(b[2]);
    for (std::uint64_t i = 0; i < w; ++i) C[i] = A[i] * B[i];
}
// 3-D stand-in via the DispatchShape registration overload: one write per thread of the full
// (normalized) grid, so it observes exactly the thread volume either dispatch form launches.
static void iota3d(void** b, unsigned n, const emu::DispatchShape& shape) {
    if (n < 1) return;
    auto* C = static_cast<float*>(b[0]);
    const auto& t = shape.threads;
    for (unsigned long z = 0; z < t.depth; ++z)
        for (unsigned long y = 0; y < t.height; ++y)
            for (unsigned long x = 0; x < t.width; ++x)
                C[z * t.width * t.height + y * t.width + x] =
                    float(z * t.width * t.height + y * t.width + x);
}
#endif

// Run one named kernel of the library over A,B -> C and check against `expect(a,b)`.
static bool run_kernel(mtl::Device* dev, mtl::Library* lib, mtl::CommandQueue* queue,
                       const char* name, float (*expect)(float, float)) {
    const std::uint32_t N = 8;
    mtl::Error* err = nullptr;
    mtl::String* fname = mtl::String::string(name, mtl::UTF8StringEncoding);
    mtl::Function* fn = lib->newFunction(fname);
    mtl::ComputePipelineState* pso = dev->newComputePipelineState(fn, &err);

    mtl::Buffer* ba = dev->newBuffer(N * sizeof(float), mtl::ResourceStorageModeShared);
    mtl::Buffer* bb = dev->newBuffer(N * sizeof(float), mtl::ResourceStorageModeShared);
    mtl::Buffer* bc = dev->newBuffer(N * sizeof(float), mtl::ResourceStorageModeShared);
    auto* A = static_cast<float*>(ba->contents());
    auto* B = static_cast<float*>(bb->contents());
    for (std::uint32_t i = 0; i < N; ++i) { A[i] = float(i) + 1.0f; B[i] = float(i) * 3.0f; }

    mtl::CommandBuffer* cb = queue->commandBuffer();
    mtl::ComputeCommandEncoder* enc = cb->computeCommandEncoder();
    enc->setComputePipelineState(pso);
    enc->setBuffer(ba, 0, 0); enc->setBuffer(bb, 0, 1); enc->setBuffer(bc, 0, 2);
    enc->dispatchThreads(mtl::Size(N, 1, 1), mtl::Size(N, 1, 1));
    enc->endEncoding();
    cb->commit();
    cb->waitUntilCompleted();

    auto* C = static_cast<const float*>(bc->contents());
    bool ok = true;
    for (std::uint32_t i = 0; i < N; ++i) {
        const float want = expect(A[i], B[i]);
        if (C[i] != want) { ok = false; std::printf("  %s mismatch[%u]: %g != %g\n", name, i, C[i], want); }
    }
    std::printf("  %s: ", name);
    for (std::uint32_t i = 0; i < N; ++i) std::printf("%g ", C[i]);
    std::printf("%s\n", ok ? "(ok)" : "(FAIL)");

    ba->release(); bb->release(); bc->release(); pso->release(); fn->release();
    return ok;
}

static float add(float a, float b) { return a + b; }
static float mul(float a, float b) { return a * b; }

// Run the 3-D iota kernel over an 8x8x1 thread volume through ONE of the two dispatch forms and
// check every element — proves y/z extents reach the kernel, and that dispatchThreadgroups
// launches groups x threadsPerThreadgroup threads (not group counts).
static bool run_iota3d(mtl::Device* dev, mtl::Library* lib, mtl::CommandQueue* queue, bool by_groups) {
    const std::uint32_t W = 8, H = 8;
    mtl::Error* err = nullptr;
    mtl::String* fname = mtl::String::string("iota3d", mtl::UTF8StringEncoding);
    mtl::Function* fn = lib->newFunction(fname);
    mtl::ComputePipelineState* pso = dev->newComputePipelineState(fn, &err);

    mtl::Buffer* bc = dev->newBuffer(W * H * sizeof(float), mtl::ResourceStorageModeShared);

    mtl::CommandBuffer* cb = queue->commandBuffer();
    mtl::ComputeCommandEncoder* enc = cb->computeCommandEncoder();
    enc->setComputePipelineState(pso);
    enc->setBuffer(bc, 0, 0);
    if (by_groups) {
        enc->dispatchThreadgroups(mtl::Size(2, 2, 1), mtl::Size(4, 4, 1));  // 2x2 groups of 4x4 = 8x8
    } else {
        enc->dispatchThreads(mtl::Size(W, H, 1), mtl::Size(4, 4, 1));
    }
    enc->endEncoding();
    cb->commit();
    cb->waitUntilCompleted();

    auto* C = static_cast<const float*>(bc->contents());
    bool ok = true;
    for (std::uint32_t i = 0; i < W * H; ++i) {
        if (C[i] != float(i)) { ok = false; std::printf("  iota3d mismatch[%u]: %g != %g\n", i, C[i], double(i)); }
    }
    std::printf("  iota3d (%s): %s\n", by_groups ? "dispatchThreadgroups" : "dispatchThreads",
                ok ? "ok" : "FAIL");

    bc->release(); pso->release(); fn->release();
    return ok;
}

int main() {
#ifndef __APPLE__
    emu::register_kernel("add_arrays", &add_arrays);
    emu::register_kernel("mul_arrays", &mul_arrays);
    emu::register_kernel("iota3d", &iota3d);
    std::printf("Metal compute on the SOFTWARE-EMULATED device:\n");
#else
    std::printf("Metal compute on REAL Apple hardware:\n");
#endif
    bool ok = true;
    {
        mtl::AutoreleasePool* pool = mtl::AutoreleasePool::alloc()->init();
        mtl::Device* dev = mtl::CreateSystemDefaultDevice();
        if (!dev) { std::printf("RESULT: FAIL (no Metal device)\n"); return 1; }
        mtl::CommandQueue* queue = dev->newCommandQueue();
        mtl::Error* err = nullptr;
        mtl::String* src = mtl::String::string(kSource, mtl::UTF8StringEncoding);
        mtl::Library* lib = dev->newLibrary(src, static_cast<const mtl::CompileOptions*>(nullptr), &err);
        if (!lib) { std::printf("RESULT: FAIL (library did not compile)\n"); return 1; }

        ok &= run_kernel(dev, lib, queue, "add_arrays", &add);
        ok &= run_kernel(dev, lib, queue, "mul_arrays", &mul);
        ok &= run_iota3d(dev, lib, queue, false);  // dispatchThreads with a 2-D grid
        ok &= run_iota3d(dev, lib, queue, true);   // dispatchThreadgroups, normalized to threads

        lib->release(); queue->release(); dev->release();
        pool->release();
    }
#ifndef __APPLE__
    const unsigned long leaked = emu::live_objects();
    if (leaked != 0) { ok = false; std::printf("  LEAK: %lu emulator objects still alive\n", leaked); }
#endif
    std::printf("RESULT: %s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
