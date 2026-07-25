// metal_texture_test.cpp — the GRAPHICS half of the Metal surface: create a 2D texture, clear it
// through a render pass (loadAction = Clear), and read the pixels back. The same source runs on real
// Apple Metal and, off Apple, on the software-emulated device (gpu/metal/emulated/), which is the
// Metal analogue of llvmpipe.
//
// This mirrors the Vulkan offscreen path exactly, so a consumer's "open a device, clear a target,
// read it back" layer produces byte-identical pixels on both backends: RGBA8Unorm, bytes in memory
// R,G,B,A, and a clear of (0.25, 0.5, 0.75, 1.0) reads back as (64, 128, 191, 255).
//
// This suite drives raw metal-cpp (MTL::) on purpose — the escape-hatch spelling a cpp{} block
// uses — where the compute/multiline suites drive the mtl.* facade (aliases of the same types).
#include "harness.hpp"

#include <Metal/Metal.hpp>

#include <cstdint>
#include <vector>

namespace cheatah::gpu::mtltest {
namespace {

class MetalTexture : public MetalTest {
  protected:
    static constexpr NS::UInteger kWidth = 64;
    static constexpr NS::UInteger kHeight = 64;

    // The RGBA8 bytes a clear of (0.25, 0.5, 0.75, 1.0) must produce, correctly rounded — packed
    // (r<<24)|(g<<16)|(b<<8)|a, byte-for-byte what the Vulkan path produces for the same clear.
    static constexpr std::uint32_t kWant = (64u << 24) | (128u << 16) | (191u << 8) | 255u;

    // A 64x64 RGBA8 render target, host-readable.
    MTL::Texture* NewTarget() {
        MTL::TextureDescriptor* desc = MTL::TextureDescriptor::alloc()->init();
        desc->setTextureType(MTL::TextureType2D);
        desc->setPixelFormat(MTL::PixelFormatRGBA8Unorm);
        desc->setWidth(kWidth);
        desc->setHeight(kHeight);
        desc->setUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead);
        desc->setStorageMode(MTL::StorageModeShared);
        MTL::Texture* target = device_->newTexture(desc);
        desc->release();
        return target;
    }

    // Clear the target through a render pass — the Metal way to fill a target.
    void ClearTarget(MTL::Texture* target) {
        MTL::RenderPassDescriptor* pass = MTL::RenderPassDescriptor::alloc()->init();
        MTL::RenderPassColorAttachmentDescriptor* color = pass->colorAttachments()->object(0);
        color->setTexture(target);
        color->setLoadAction(MTL::LoadActionClear);
        color->setStoreAction(MTL::StoreActionStore);
        color->setClearColor(MTL::ClearColor(0.25, 0.5, 0.75, 1.0));

        MTL::CommandBuffer* cmd = queue_->commandBuffer();
        MTL::RenderCommandEncoder* enc = cmd->renderCommandEncoder(pass);
        ASSERT_NE(enc, nullptr) << "renderCommandEncoderWithDescriptor";
        enc->endEncoding();
        cmd->commit();
        cmd->waitUntilCompleted();
        pass->release();
    }

    /// The packed pixel at (x, y) of a tightly-packed RGBA8 readback: (r<<24)|(g<<16)|(b<<8)|a.
    static std::uint32_t PackedAt(const std::vector<std::uint8_t>& px, NS::UInteger x, NS::UInteger y) {
        const std::size_t i = (y * kWidth + x) * 4;
        return (static_cast<std::uint32_t>(px[i + 0]) << 24) | (static_cast<std::uint32_t>(px[i + 1]) << 16) |
               (static_cast<std::uint32_t>(px[i + 2]) << 8) | static_cast<std::uint32_t>(px[i + 3]);
    }
};

TEST_F(MetalTexture, ReportsItsExtent) {
    MTL::Texture* target = NewTarget();
    ASSERT_NE(target, nullptr) << "newTextureWithDescriptor";
    EXPECT_EQ(target->width(), kWidth);
    EXPECT_EQ(target->height(), kHeight);
    target->release();
}

// Clear -> full readback: the corner pixels and EVERY pixel must be exactly (64, 128, 191, 255).
TEST_F(MetalTexture, ClearReadsBackExactBytes) {
    MTL::Texture* target = NewTarget();
    ASSERT_NE(target, nullptr);
    ASSERT_NO_FATAL_FAILURE(ClearTarget(target));

    std::vector<std::uint8_t> pixels(kWidth * kHeight * 4, 0);
    target->getBytes(pixels.data(), kWidth * 4, MTL::Region(0, 0, kWidth, kHeight), 0);

    EXPECT_EQ(PackedAt(pixels, 0, 0), kWant);
    EXPECT_EQ(PackedAt(pixels, kWidth - 1, kHeight - 1), kWant);

    unsigned mismatches = 0;
    for (NS::UInteger y = 0; y < kHeight; ++y)
        for (NS::UInteger x = 0; x < kWidth; ++x)
            if (PackedAt(pixels, x, y) != kWant) ++mismatches;
    EXPECT_EQ(mismatches, 0u) << "pixels of the cleared target differ from the clear color";

    target->release();
}

// A sub-region readback must see the same pixels (proves the row-stride maths).
TEST_F(MetalTexture, SubRegionReadbackHonoursOriginAndStride) {
    MTL::Texture* target = NewTarget();
    ASSERT_NE(target, nullptr);
    ASSERT_NO_FATAL_FAILURE(ClearTarget(target));

    std::vector<std::uint8_t> corner(2 * 2 * 4, 0);
    target->getBytes(corner.data(), 2 * 4, MTL::Region(1, 1, 2, 2), 0);
    EXPECT_EQ(corner[0], 64);
    EXPECT_EQ(corner[1], 128);
    EXPECT_EQ(corner[2], 191);
    EXPECT_EQ(corner[3], 255);

    target->release();
}

}  // namespace
}  // namespace cheatah::gpu::mtltest
