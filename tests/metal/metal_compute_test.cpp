// metal_compute_test.cpp — Metal compute, the SAME tests on real Apple hardware and on the software
// emulator. They carry real Metal Shading Language (MSL) source: on Apple, Metal compiles it and the
// kernels run on the GPU; off Apple, the software device (gpu/metal/emulated) ignores the MSL and runs
// the registered C++ stand-ins on the CPU instead. Either way the canonical flow is exercised — device
// -> library -> function -> pipeline -> queue -> command buffer -> compute encoder -> setBuffer ->
// dispatchThreads -> commit -> read contents() — and the results are checked bit-for-bit. Two kernels
// (add, multiply) give "a couple" of real-hardware checks; iota3d proves the 3-D dispatch extents.
// Leak-clean: everything owned is released, and the fixture's TearDown asserts it (emulator builds).
#include "harness.hpp"

#include <array>
#include <cstdint>

namespace cheatah::gpu::mtltest {
namespace {

// Real MSL — compiled by Metal on Apple; ignored by the emulator (which dispatches by function name).
const char* kSource = R"MSL(
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
namespace emu = cheatah::gpu::metal::emulated;

// CPU stand-ins for the emulator, matched to the kernels above by name.
void add_arrays(void** b, unsigned n, unsigned long w) {
    if (n < 3) return;
    auto* A = static_cast<const float*>(b[0]); auto* B = static_cast<const float*>(b[1]); auto* C = static_cast<float*>(b[2]);
    for (std::uint64_t i = 0; i < w; ++i) C[i] = A[i] + B[i];
}
void mul_arrays(void** b, unsigned n, unsigned long w) {
    if (n < 3) return;
    auto* A = static_cast<const float*>(b[0]); auto* B = static_cast<const float*>(b[1]); auto* C = static_cast<float*>(b[2]);
    for (std::uint64_t i = 0; i < w; ++i) C[i] = A[i] * B[i];
}
// 3-D stand-in via the DispatchShape registration overload: one write per thread of the full
// (normalized) grid, so it observes exactly the thread volume either dispatch form launches.
void iota3d(void** b, unsigned n, const emu::DispatchShape& shape) {
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

class MetalCompute : public MetalTest {
  protected:
    static constexpr std::uint32_t kN = 8;        // 1-D kernels: elements per array
    static constexpr std::uint32_t kW = 8, kH = 8; // iota3d: the 8x8x1 thread volume

    static void SetUpTestSuite() {
#ifndef __APPLE__
        emu::register_kernel("add_arrays", &add_arrays);
        emu::register_kernel("mul_arrays", &mul_arrays);
        emu::register_kernel("iota3d", &iota3d);
#endif
    }

    void SetUp() override {
        MetalTest::SetUp();
        mtl::Error* err = nullptr;
        mtl::String* src = mtl::String::string(kSource, mtl::UTF8StringEncoding);
        library_ = device_->newLibrary(src, static_cast<const mtl::CompileOptions*>(nullptr), &err);
        ASSERT_NE(library_, nullptr) << "MSL library did not compile";
    }

    void TearDown() override {
        if (library_ != nullptr) library_->release();
        MetalTest::TearDown();
    }

    // Run one named kernel of the library over A,B -> C (kN elements) and return all three arrays.
    void RunKernel(const char* name, std::array<float, kN>& A_out, std::array<float, kN>& B_out,
                   std::array<float, kN>& C_out) {
        mtl::Error* err = nullptr;
        mtl::String* fname = mtl::String::string(name, mtl::UTF8StringEncoding);
        mtl::Function* fn = library_->newFunction(fname);
        ASSERT_NE(fn, nullptr) << name;
        mtl::ComputePipelineState* pso = device_->newComputePipelineState(fn, &err);
        ASSERT_NE(pso, nullptr) << name;

        mtl::Buffer* ba = device_->newBuffer(kN * sizeof(float), mtl::ResourceStorageModeShared);
        mtl::Buffer* bb = device_->newBuffer(kN * sizeof(float), mtl::ResourceStorageModeShared);
        mtl::Buffer* bc = device_->newBuffer(kN * sizeof(float), mtl::ResourceStorageModeShared);
        auto* A = static_cast<float*>(ba->contents());
        auto* B = static_cast<float*>(bb->contents());
        for (std::uint32_t i = 0; i < kN; ++i) { A[i] = float(i) + 1.0f; B[i] = float(i) * 3.0f; }

        mtl::CommandBuffer* cb = queue_->commandBuffer();
        mtl::ComputeCommandEncoder* enc = cb->computeCommandEncoder();
        enc->setComputePipelineState(pso);
        enc->setBuffer(ba, 0, 0); enc->setBuffer(bb, 0, 1); enc->setBuffer(bc, 0, 2);
        enc->dispatchThreads(mtl::Size(kN, 1, 1), mtl::Size(kN, 1, 1));
        enc->endEncoding();
        cb->commit();
        cb->waitUntilCompleted();

        auto* C = static_cast<const float*>(bc->contents());
        for (std::uint32_t i = 0; i < kN; ++i) { A_out[i] = A[i]; B_out[i] = B[i]; C_out[i] = C[i]; }

        ba->release(); bb->release(); bc->release(); pso->release(); fn->release();
    }

    // Run the 3-D iota kernel over an 8x8x1 thread volume through ONE of the two dispatch forms and
    // return every element — proves y/z extents reach the kernel, and that dispatchThreadgroups
    // launches groups x threadsPerThreadgroup threads (not group counts).
    void RunIota3d(bool by_groups, std::array<float, kW * kH>& C_out) {
        mtl::Error* err = nullptr;
        mtl::String* fname = mtl::String::string("iota3d", mtl::UTF8StringEncoding);
        mtl::Function* fn = library_->newFunction(fname);
        ASSERT_NE(fn, nullptr);
        mtl::ComputePipelineState* pso = device_->newComputePipelineState(fn, &err);
        ASSERT_NE(pso, nullptr);

        mtl::Buffer* bc = device_->newBuffer(kW * kH * sizeof(float), mtl::ResourceStorageModeShared);

        mtl::CommandBuffer* cb = queue_->commandBuffer();
        mtl::ComputeCommandEncoder* enc = cb->computeCommandEncoder();
        enc->setComputePipelineState(pso);
        enc->setBuffer(bc, 0, 0);
        if (by_groups) {
            enc->dispatchThreadgroups(mtl::Size(2, 2, 1), mtl::Size(4, 4, 1));  // 2x2 groups of 4x4 = 8x8
        } else {
            enc->dispatchThreads(mtl::Size(kW, kH, 1), mtl::Size(4, 4, 1));
        }
        enc->endEncoding();
        cb->commit();
        cb->waitUntilCompleted();

        auto* C = static_cast<const float*>(bc->contents());
        for (std::uint32_t i = 0; i < kW * kH; ++i) C_out[i] = C[i];

        bc->release(); pso->release(); fn->release();
    }

    mtl::Library* library_ = nullptr;
};

TEST_F(MetalCompute, AddArrays) {
    std::array<float, kN> A{}, B{}, C{};
    ASSERT_NO_FATAL_FAILURE(RunKernel("add_arrays", A, B, C));
    for (std::uint32_t i = 0; i < kN; ++i) EXPECT_EQ(C[i], A[i] + B[i]) << "index " << i;
}

TEST_F(MetalCompute, MulArrays) {
    std::array<float, kN> A{}, B{}, C{};
    ASSERT_NO_FATAL_FAILURE(RunKernel("mul_arrays", A, B, C));
    for (std::uint32_t i = 0; i < kN; ++i) EXPECT_EQ(C[i], A[i] * B[i]) << "index " << i;
}

// dispatchThreads with a 2-D grid: every element of the 8x8 volume written with its linear index.
TEST_F(MetalCompute, Grid3d) {
    std::array<float, kW * kH> C{};
    ASSERT_NO_FATAL_FAILURE(RunIota3d(false, C));
    for (std::uint32_t i = 0; i < kW * kH; ++i) EXPECT_EQ(C[i], float(i)) << "index " << i;
}

// dispatchThreadgroups, normalized to threads: 2x2 groups of 4x4 must launch the SAME 8x8 volume.
TEST_F(MetalCompute, Threadgroups) {
    std::array<float, kW * kH> C{};
    ASSERT_NO_FATAL_FAILURE(RunIota3d(true, C));
    for (std::uint32_t i = 0; i < kW * kH; ++i) EXPECT_EQ(C[i], float(i)) << "index " << i;
}

#ifndef __APPLE__
// The emulator's leak counter itself: creating an object raises live_objects() by exactly one,
// releasing it restores the count. (The ==0 end-state check runs in every test's TearDown.)
TEST_F(MetalCompute, LeakClean) {
    const unsigned long base = emu::live_objects();
    EXPECT_GT(base, 0ul) << "pool/device/queue/library should be alive and counted";
    mtl::Buffer* b = device_->newBuffer(16, mtl::ResourceStorageModeShared);
    EXPECT_EQ(emu::live_objects(), base + 1);
    b->release();
    EXPECT_EQ(emu::live_objects(), base);
}
#endif

}  // namespace
}  // namespace cheatah::gpu::mtltest
