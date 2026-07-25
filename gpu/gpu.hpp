#pragma once

/**
 * @file gpu.hpp
 * @brief `import gpu` — the package header: the NATIVE GPU APIs, generated 1:1, typed for cheatah.
 *
 * cheatah-gpu is deliberately a THIN library. It exposes each native GPU API as faithfully as the
 * API itself, and does exactly one thing on top: it fixes the typing, so cheatah's numbers reach a
 * C API that wants exact widths. Every generated forwarder has a cheatah-friendly overload — pass a
 * `long long` where Vulkan wants a handle or a `uint32_t`/`VkDeviceSize`, a `double` where it wants
 * a `float`, and the cast is done for you (see `vulkan/commands.hpp`; `vulkan/handles.hpp` names the
 * reverse direction, for out-params, handle arrays, and native struct fields).
 *
 * There is **no easy/simplified layer here, by design.** An ergonomic "open a device, clear a
 * target, read it back" surface is a policy decision — how much is synchronous, who owns memory,
 * what a frame is — and belongs to the CONSUMER (a renderer, an engine, a compute app), which can
 * build exactly the layer it wants on top of these surfaces. cheatah-gpu stays the honest ground.
 *
 * Likewise, bringing up a **window** is not this library's job — that is project-specific (GLFW,
 * SDL, native); the consumer supplies a finished surface handle and cheatah-gpu hands it the
 * surface/swapchain primitives.
 *
 * Submodules:
 *   - gpu.backend  — compile-time backend selection + the shared-interface conventions.  [working]
 *   - gpu.dispatch — compute-shader dispatch-dimensioning math (pure integer).            [working]
 *   - gpu.vulkan   — Vulkan backend, true to the Vulkan C API (volk + VMA).               [shipped]
 *   - gpu.metal    — native Metal backend for Apple platforms.                            [shipped]
 *
 * The backend is chosen at COMPILE TIME (see @ref backend.hpp), so a binary only ever carries the
 * API it actually uses — the `#if` below is how one backend's surface is included without pulling in
 * the other API's bloat. `import gpu` gives you the backend switch, the dispatch math, and the
 * active backend's native surface; `import gpu.vulkan` / `import gpu.metal` name one directly.
 */

#include "backend.hpp"
#include "dispatch/dispatch.hpp"

// Pull in the native backend's full surface — its `vk*` forwarders AND every Vulkan struct + handle
// the user creates (VkBufferCreateInfo, VkBuffer, …) — when its headers are available, so
// `import gpu.vulkan` just works (construct structs, take their address, call; no cpp{} needed). A
// build without those headers still gets gpu.dispatch / gpu.backend (headless, zero dependencies).
#ifdef CHEATAH_GPU_BACKEND_VULKAN
#  if __has_include(<volk.h>) || __has_include(<vulkan/vulkan.h>)
#    include "vulkan/commands.hpp"
#    include "vulkan/handles.hpp"  // native handle <-> cheatah `long long` token
#  endif
#endif
// Pull in the native Metal surface — every `mtl.<Name>` alias (mtl.Device, mtl.Buffer, …) — when
// metal-cpp is available, so `import gpu.metal as mtl` just works (call Metal the same way as Vulkan).
#ifdef CHEATAH_GPU_BACKEND_METAL
#  if __has_include(<Metal/Metal.hpp>)
#    include "metal/types.hpp"
#    include "metal/handles.hpp"  // native object <-> cheatah `long long` token
#  endif
#endif

namespace cheatah::gpu {}
