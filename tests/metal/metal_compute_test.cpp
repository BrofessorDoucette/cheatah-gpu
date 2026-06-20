// metal_compute_test.cpp — Metal compute, the SAME test on real Apple hardware and on the software
// emulator. It carries real Metal Shading Language (MSL) source: on Apple, Metal compiles it and the
// kernels run on the GPU; off Apple, the software device (gpu/metal/emulated) ignores the MSL and runs
// the registered C++ stand-ins on the CPU instead. Either way the canonical flow is exercised — device
// -> library -> function -> pipeline -> queue -> command buffer -> compute encoder -> setBuffer ->
// dispatchThreads -> commit -> read contents() — and the results are checked bit-for-bit. Two kernels
// (add, multiply) give "a couple" of real-hardware checks. Leak-clean: everything owned is released.
#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include <cstdint>
#include <cstdio>
#include <vector>

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
#endif

// Run one named kernel of the library over A,B -> C and check against `expect(a,b)`.
static bool run_kernel(MTL::Device* dev, MTL::Library* lib, MTL::CommandQueue* queue,
                       const char* name, float (*expect)(float, float)) {
    const std::uint32_t N = 8;
    NS::Error* err = nullptr;
    NS::String* fname = NS::String::string(name, NS::UTF8StringEncoding);
    MTL::Function* fn = lib->newFunction(fname);
    MTL::ComputePipelineState* pso = dev->newComputePipelineState(fn, &err);

    MTL::Buffer* ba = dev->newBuffer(N * sizeof(float), MTL::ResourceStorageModeShared);
    MTL::Buffer* bb = dev->newBuffer(N * sizeof(float), MTL::ResourceStorageModeShared);
    MTL::Buffer* bc = dev->newBuffer(N * sizeof(float), MTL::ResourceStorageModeShared);
    auto* A = static_cast<float*>(ba->contents());
    auto* B = static_cast<float*>(bb->contents());
    for (std::uint32_t i = 0; i < N; ++i) { A[i] = float(i) + 1.0f; B[i] = float(i) * 3.0f; }

    MTL::CommandBuffer* cb = queue->commandBuffer();
    MTL::ComputeCommandEncoder* enc = cb->computeCommandEncoder();
    enc->setComputePipelineState(pso);
    enc->setBuffer(ba, 0, 0); enc->setBuffer(bb, 0, 1); enc->setBuffer(bc, 0, 2);
    enc->dispatchThreads(MTL::Size(N, 1, 1), MTL::Size(N, 1, 1));
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

int main() {
#ifndef __APPLE__
    emu::register_kernel("add_arrays", &add_arrays);
    emu::register_kernel("mul_arrays", &mul_arrays);
    std::printf("Metal compute on the SOFTWARE-EMULATED device:\n");
#else
    std::printf("Metal compute on REAL Apple hardware:\n");
#endif
    bool ok = true;
    {
        NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
        MTL::Device* dev = MTL::CreateSystemDefaultDevice();
        if (!dev) { std::printf("RESULT: FAIL (no Metal device)\n"); return 1; }
        MTL::CommandQueue* queue = dev->newCommandQueue();
        NS::Error* err = nullptr;
        NS::String* src = NS::String::string(kSource, NS::UTF8StringEncoding);
        MTL::Library* lib = dev->newLibrary(src, static_cast<const MTL::CompileOptions*>(nullptr), &err);
        if (!lib) { std::printf("RESULT: FAIL (library did not compile)\n"); return 1; }

        ok &= run_kernel(dev, lib, queue, "add_arrays", &add);
        ok &= run_kernel(dev, lib, queue, "mul_arrays", &mul);

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
