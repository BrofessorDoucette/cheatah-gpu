# cheatah-gpu — design & architecture agreements

These are the load-bearing decisions cheatah-gpu honors **at all times**. They are intentionally
recorded here (outside `gpu/`, so the doc-coverage gate doesn't treat prose as API). The code under
`gpu/` now implements them — the generated Vulkan surface, the native Metal backend, and the
dispatch/backend core are all shipped — and these notes remain the binding contract that
implementation honors.

## The native surfaces, and nothing above them

| `import …` | audience | contract |
|------------|----------|----------|
| **`gpu.vulkan`** | native Vulkan engineers | kept **as true to the native Vulkan C API as possible** — a Vulkan graphics engineer should feel at home. No lowest-common-denominator wrapper. |
| **`gpu.metal`** | native Metal engineers | kept **as true to the native Metal API as possible** — same idea, for Apple. |
| **`gpu`** | everyone | the **package header**: the compile-time backend switch, the dispatch math, and the active backend's native surface. |

**There is no "easy" layer here, and there will not be one.** cheatah-gpu exposes each native API
faithfully and does exactly one thing on top: it **fixes the typing**, so cheatah's numbers reach a C
API that wants exact widths. Every generated forwarder carries a cheatah-friendly overload — pass a
`long long` where Vulkan wants a handle or a `uint32_t`/`VkDeviceSize`, a `double` where it wants a
`float`, and the cast is done for you. `gpu/vulkan/handles.hpp` (and its Metal mirror) names the
reverse direction, for the three places a token cannot go: out-params, handle arrays, and native
struct fields.

Everything above that — what "open a device, clear a target, read it back" means, what is
synchronous, who owns memory, what a frame is — is **policy**, and policy belongs to the **consumer**
(a renderer, an engine, a compute app), which builds exactly the layer it wants on these surfaces.
cheatah-gpu stays the honest ground. This is why a consuming engine's ergonomic GPU layer lives in
that engine, not here.

The backend is selected at **compile time** (`gpu/backend.hpp`), so a binary never carries code for
an API it isn't using.

## Concurrency & memory ownership

1. **cheatah-gpu does no multithreading of its own.** This is a cheatah repo — *the user* decides
   their threading model. We never spawn threads behind the user's back.
2. **We impose no synchronization model.** The native surfaces expose each API's own synchronization
   primitives (fences, semaphores, barriers, command buffers) unchanged, so a native engineer is
   never boxed in and never has to fight a wrapper's idea of a frame.
3. **Memory is the user's to manage; we never hide it.** What we do instead is make the ownership
   contract *impossible to miss*: every call that hands back a resource carries an `@destroy` tag
   naming exactly what must be released and how, and allocation is tagged `@alloc` (host) vs
   `@gpualloc` (device).

Async transfer, no-copy array borrowing, and lifetime-safe leases are **real problems worth solving —
just not here.** They are policy, and policy belongs to the consumer that builds an ergonomic layer
on these surfaces (motivated, originally, by a refusal to repeat Unity's painful asynchronous-GPU
API). A consumer wanting a lifetime-safe borrow already has the vocabulary for it in cheatah's
`memory` module (`Owner` / `Lease` / `Request`): pin a `ndarray`, hand the backend a non-owning view,
release when the completion fence signals, and never transfer ownership. cheatah-gpu's job is to make
that layer *possible and cheap to write*, not to pick its rules.

## Provisioning & windowing (recap)

- **Setup is the build's job.** `biome add cheatah-gpu` → `scripts/install-deps.sh` provisions the
  userspace GPU stack (Vulkan loader/layers, Slang); `scripts/doctor.sh` verifies it. Kernel GPU
  drivers are detected and guided, never force-installed.
- **Windowing is out of scope** — a consumer concern (GLFW vs SDL). cheatah-gpu provides
  surface/swapchain primitives so a windowing extension brings a window up in a breeze; GLFW is a
  test-only dependency here.

## Platform support

- **Linux** (apt/dnf/pacman) and **macOS** (brew; native Metal preferred, MoltenVK fallback) are
  first-class today.
- **Windows** is a **roadmap side quest** — not a current priority. `scripts/install-deps.sh` prints
  manual guidance there for now (Vulkan SDK + driver + optional GLFW); a winget/vcpkg one-shot lands
  later. The Vulkan C-API + volk + Slang stack is already Windows-portable, so it's provisioning, not
  porting.

## `gpu.vulkan` — the full native surface (binding plan)

`gpu.vulkan` exposes the whole Vulkan API to cheatah developers, faithfully.

- **Generated, not hand-written.** A generator (`tools/vulkan-gen/`) reads the **vendored** registry
  `tools/vulkan-gen/vk.xml` (committed → hermetic + version-pinned; **no user ever hits a "vk.xml
  missing" error**, and the generated header is committed too) and emits an **`inline` forwarder per
  command** that calls the real `vk*` entry point through **volk**'s loaded pointers — so it is the
  bare Vulkan call at runtime, no overhead, no Vulkan-Hpp templates. Platform-guarded commands
  (`VK_USE_PLATFORM_*`) are `#ifdef`-gated. Memory goes through **VMA**; shaders through **Slang**.
- **Memory is the user's to manage** — we never hide it. But the **ownership contract is explicit**:
  every call that hands back a resource carries an **`@destroy`** tag naming exactly what must be
  released and how (e.g. `@destroy release with destroy_instance(instance)`), surfaced in the comment
  AND the VS Code hover DB so the delete-contract is impossible to miss.
- **Two allocation tags** (GPU code allocates in two places): **`@alloc`** = host/CPU memory,
  **`@gpualloc`** = device/GPU memory; a call that does both (e.g. a staging upload) carries both.
- **Coverage & tests.** Each exposed function gets **≥1 cheatah `.purr` system test** and **≥2–3 C++
  unit tests with different inputs**, plus a **capability-enumeration** test asserting our surface
  covers what each device reports (`vkGetPhysicalDevice*`: features, formats, limits, extensions).
  The 100% coverage denominator is **exposed ∧ device-supported**, grown to the full supported
  surface; functions a device can't support are excluded **and logged** (never a silent cap).
- **Device matrix.** Coverage runs against three physical devices a single instance enumerates here:
  **llvmpipe** (Mesa lavapipe, software), **Intel Iris Xe** (Mesa), and **NVIDIA RTX 3070 Ti**
  (proprietary) — software + integrated + discrete, both major Linux drivers. Per-device supported
  surfaces differ (e.g. ray tracing on NVIDIA), so the denominator is per-device.
- **Layering.** This 1:1 surface is the whole product — there is nothing above it here. What makes it
  usable from cheatah is the **typing fix**: alongside each faithful forwarder, a cheatah-friendly
  overload takes plain cheatah numbers (`long long`, `double`) and casts them to the exact Vulkan
  widths and handles. So a `.purr` program (or a consumer's C++ shim) holds integer tokens, calls the
  forwarders directly, and writes no casts. Any ergonomic "device / target / clear / readback" layer
  is the consumer's to build and own.
