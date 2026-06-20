# gpu.vulkan — Vulkan backend behind the shared GPU interface  ·  status: **roadmap**

`import gpu.vulkan` is the **power-user interface**: kept **as true to the native Vulkan C API as
possible** so developers who want to be picky get the real thing, not a lowest-common-denominator
wrapper. (The easy, cross-platform layer is `import gpu` — built on top of whichever backend the
build selected; this module is what it sits on when the target is Vulkan.) It exposes **as much of
the modern Vulkan API as possible** so a user feels free.

> This directory is an **outline**. No compiled headers live here yet, so the QA gate stays scoped
> to the documented, tested seed ([`../dispatch`](../dispatch)). The decisions below are locked so
> implementation can proceed directly.

## Architecture decisions (locked)

- **Vulkan C API only.** We bind the C headers (`vulkan/vulkan.h`), not Vulkan-Hpp. The C API avoids
  template/codegen bloat in the generated cheatah C++ and tracks the spec's far better documentation
  (<https://docs.vulkan.org/spec/latest/>).
- **[volk](https://github.com/zeux/volk) meta-loader.** All Vulkan entry points load dynamically
  through volk — no link-time loader dependency, and faster device-level dispatch (`volkLoadDevice`).
- **[VMA](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator).** All device memory
  goes through the Vulkan Memory Allocator — no hand-rolled `vkAllocateMemory` sprawl.
- **[Slang](https://shader-slang.org/) shaders → SPIR-V** via `slangc` (bundled in the Vulkan SDK),
  compiled at runtime where convenient. Slang is more modern than GLSL and the intended authoring
  language for cheatah users' compute kernels.
- **Vulkan 1.3 baseline**: dynamic rendering (no render-pass objects), buffer device address (pointer
  access to buffers), descriptor indexing (bindless), synchronization2 + timeline semaphores, and
  frames-in-flight with persistently-mapped staging buffers.

## Bring-up order (what the backend automates)
1. `vkCreateInstance` (+ extensions) → `volkLoadInstance`
2. enumerate + select `VkPhysicalDevice` (prefer discrete; require the 1.3 features)
3. `vkCreateDevice` → `volkLoadDevice`
4. create the **VMA** allocator (buffer-device-address enabled)
5. *(optional, presentation)* accept a caller's surface and build a swapchain
6. per-frame command/sync objects

## Windowing is out of scope — by design
cheatah-gpu does **not** own a window. Windowing is project-specific (some users want GLFW, others
SDL), so it belongs to the consuming extension/app. What this module provides is **presentation
primitives**: given a `VkSurfaceKHR` (or a native window handle the caller already has), it creates
and manages the swapchain and present path. That makes "bring up a window and present" a breeze for
any windowing extension built on top of cheatah-gpu — without binding cheatah-gpu to one window lib.

**GLFW is a test-only dependency**: the test suite uses it to obtain a surface so the presentation
primitives can be exercised; it is never a runtime requirement of the library.

## Dependencies & provisioning
`volk` + `VMA` are core (CMake-fetched). The Vulkan **loader + drivers** are system-level:
`scripts/install-deps.sh` provisions them and `scripts/doctor.sh` verifies them. On macOS the native
**Metal** backend ([`../metal`](../metal)) is preferred; **MoltenVK** is the fallback only when Metal
is unavailable. GLFW is fetched only for the presentation tests.
