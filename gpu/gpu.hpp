#pragma once

/**
 * @file gpu.hpp
 * @brief `import gpu` — the easy, cross-platform GPU interface.
 *
 * cheatah-gpu offers THREE interfaces, so you can have the complexity when you want it and skip it
 * when you don't:
 *
 *   - **`import gpu`** (this header) — the combined, cross-platform interface: one simple API that
 *     runs on whatever backend the build selected, easier than OpenGL. For when you just want to get
 *     on the GPU and have fun, and so higher layers can be built without worrying about portability.
 *     This is the layer with the ergonomic behaviors — asynchronous GPU read/write and **no-copy**
 *     array borrowing where the caller keeps ownership (guarded by a mutex around the CPU↔GPU
 *     interface; cheatah-gpu never threads on its own). See docs/DESIGN.md.
 *   - **`import gpu.vulkan`** — the Vulkan backend kept as true to the native Vulkan C API as
 *     possible. The full-power escape hatch for developers who want to be picky.
 *   - **`import gpu.metal`** — the native Metal backend, kept as true to the Metal API as possible.
 *
 * The easy layer is implemented on top of the backend chosen at COMPILE TIME (see @ref backend.hpp),
 * so a binary only ever carries the API it actually uses — the `#if` below is how a backend's code is
 * included without pulling in the other API's bloat.
 *
 * Submodules:
 *   - gpu.backend  — compile-time backend selection + the shared-interface conventions.  [working]
 *   - gpu.dispatch — compute-shader dispatch-dimensioning math (pure integer).            [working]
 *   - gpu.vulkan   — Vulkan backend, true to the Vulkan C API (volk + VMA).               [roadmap]
 *   - gpu.metal    — native Metal backend for Apple platforms.                            [roadmap]
 */

#include "backend.hpp"
#include "dispatch/dispatch.hpp"

#ifdef CHEATAH_GPU_BACKEND_VULKAN
// #include "vulkan/vulkan.hpp"  // compiled ONLY in Vulkan builds (no Metal bloat) — when it lands
#endif
#ifdef CHEATAH_GPU_BACKEND_METAL
// #include "metal/metal.hpp"    // compiled ONLY in Metal builds (no Vulkan bloat) — when it lands
#endif

namespace cheatah::gpu {}
