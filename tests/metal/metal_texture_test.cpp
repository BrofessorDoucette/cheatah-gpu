// metal_texture_test.cpp — the GRAPHICS half of the Metal surface: create a 2D texture, clear it
// through a render pass (loadAction = Clear), and read the pixels back. The same source runs on real
// Apple Metal and, off Apple, on the software-emulated device (gpu/metal/emulated/), which is the
// Metal analogue of llvmpipe.
//
// This mirrors the Vulkan offscreen path exactly, so a consumer's "open a device, clear a target,
// read it back" layer produces byte-identical pixels on both backends: RGBA8Unorm, bytes in memory
// R,G,B,A, and a clear of (0.25, 0.5, 0.75, 1.0) reads back as (64, 128, 191, 255).
//
// Driven by scripts/metal_gate.sh. Prints RESULT: PASS/FAIL so the gate can grep it.

#include <Metal/Metal.hpp>

#include <cstdint>
#include <cstdio>
#include <vector>

#include "gpu/metal/emulated/emulated.hpp"

namespace emu = cheatah::gpu::metal::emulated;

namespace {

constexpr NS::UInteger kWidth = 64;
constexpr NS::UInteger kHeight = 64;

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("  FAIL %s\n", what);
        ++failures;
    } else {
        std::printf("  PASS %s\n", what);
    }
}

/// The packed pixel at (x, y) of a tightly-packed RGBA8 readback: (r<<24)|(g<<16)|(b<<8)|a.
std::uint32_t packed_at(const std::vector<std::uint8_t>& px, NS::UInteger x, NS::UInteger y) {
    const std::size_t i = (y * kWidth + x) * 4;
    return (static_cast<std::uint32_t>(px[i + 0]) << 24) | (static_cast<std::uint32_t>(px[i + 1]) << 16) |
           (static_cast<std::uint32_t>(px[i + 2]) << 8) | static_cast<std::uint32_t>(px[i + 3]);
}

}  // namespace

int main() {
    std::printf("== gpu.metal: texture -> render-pass clear -> readback ==\n");

    NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();

    MTL::Device* device = MTL::CreateSystemDefaultDevice();
    check(device != nullptr, "CreateSystemDefaultDevice");
    if (device == nullptr) { std::printf("RESULT: FAIL\n"); return 1; }

    MTL::CommandQueue* queue = device->newCommandQueue();
    check(queue != nullptr, "newCommandQueue");

    // A 64x64 RGBA8 render target, host-readable.
    MTL::TextureDescriptor* desc = MTL::TextureDescriptor::alloc()->init();
    desc->setTextureType(MTL::TextureType2D);
    desc->setPixelFormat(MTL::PixelFormatRGBA8Unorm);
    desc->setWidth(kWidth);
    desc->setHeight(kHeight);
    desc->setUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead);
    desc->setStorageMode(MTL::StorageModeShared);

    MTL::Texture* target = device->newTexture(desc);
    check(target != nullptr, "newTextureWithDescriptor");
    check(target != nullptr && target->width() == kWidth && target->height() == kHeight,
          "texture reports its extent");

    // Clear it through a render pass — the Metal way to fill a target.
    MTL::RenderPassDescriptor* pass = MTL::RenderPassDescriptor::alloc()->init();
    MTL::RenderPassColorAttachmentDescriptor* color = pass->colorAttachments()->object(0);
    color->setTexture(target);
    color->setLoadAction(MTL::LoadActionClear);
    color->setStoreAction(MTL::StoreActionStore);
    color->setClearColor(MTL::ClearColor(0.25, 0.5, 0.75, 1.0));

    MTL::CommandBuffer* cmd = queue->commandBuffer();
    MTL::RenderCommandEncoder* enc = cmd->renderCommandEncoder(pass);
    check(enc != nullptr, "renderCommandEncoderWithDescriptor");
    enc->endEncoding();
    cmd->commit();
    cmd->waitUntilCompleted();

    // Read every pixel back.
    std::vector<std::uint8_t> pixels(kWidth * kHeight * 4, 0);
    target->getBytes(pixels.data(), kWidth * 4, MTL::Region(0, 0, kWidth, kHeight), 0);

    // (0.25, 0.5, 0.75, 1.0) -> RGBA8 (64, 128, 191, 255), correctly rounded — byte-for-byte what
    // the Vulkan path produces for the same clear.
    const std::uint32_t want = (64u << 24) | (128u << 16) | (191u << 8) | 255u;
    check(packed_at(pixels, 0, 0) == want, "read_pixel(0,0) == (64,128,191,255)");
    check(packed_at(pixels, kWidth - 1, kHeight - 1) == want, "read_pixel(63,63) == (64,128,191,255)");

    bool uniform = true;
    for (NS::UInteger y = 0; y < kHeight && uniform; ++y) {
        for (NS::UInteger x = 0; x < kWidth; ++x) {
            if (packed_at(pixels, x, y) != want) { uniform = false; break; }
        }
    }
    check(uniform, "every pixel of the cleared target matches");

    // A sub-region readback must see the same pixels (proves the row-stride maths).
    std::vector<std::uint8_t> corner(2 * 2 * 4, 0);
    target->getBytes(corner.data(), 2 * 4, MTL::Region(1, 1, 2, 2), 0);
    check(corner[0] == 64 && corner[1] == 128 && corner[2] == 191 && corner[3] == 255,
          "sub-region getBytes honours origin + stride");

    pass->release();
    desc->release();
    target->release();
    queue->release();
    device->release();
    pool->release();

    // The emulator counts live objects when built with -DCHEATAH_GPU_METAL_LEAKCHECK=1.
    check(emu::live_objects() == 0, "no emulator objects leaked");

    std::printf(failures == 0 ? "RESULT: PASS\n" : "RESULT: FAIL\n");
    return failures == 0 ? 0 : 1;
}
