# cheatah-gpu — design & architecture agreements

These are the load-bearing decisions cheatah-gpu honors **at all times**. They are intentionally
recorded here (outside `gpu/`, so the doc-coverage gate doesn't treat prose as API) because the code
under `gpu/` is currently an **outline** — these notes are the contract the implementation fills in.

## Three interfaces (complexity when you want it; simplicity when you don't)

| `import …` | audience | contract |
|------------|----------|----------|
| **`gpu`** | "I just want to use my GPU" | a **slim, cross-platform interface over the native APIs** — easier than OpenGL. This is where the ergonomic behaviors live (async I/O, array reuse/borrowing). Built on the compile-time-selected backend. |
| **`gpu.vulkan`** | native Vulkan engineers | kept **as true to the native Vulkan C API as possible** — a Vulkan graphics engineer should feel at home. No lowest-common-denominator wrapper. |
| **`gpu.metal`** | native Metal engineers | kept **as true to the native Metal API as possible** — same idea, for Apple. |

The backend is selected at **compile time** (`gpu/backend.hpp`), so a binary never carries code for
an API it isn't using.

## Concurrency & memory ownership

Motivated by a refusal to repeat Unity's painful asynchronous-GPU API. The agreement:

1. **cheatah-gpu does no multithreading of its own.** This is a cheatah repo — *the user* decides
   their threading model. We never spawn threads behind the user's back.
2. **Asynchronous GPU read/write is supported, and it works.** Submitting work and reading/writing
   GPU memory does not block the user into a synchronous model.
3. **No-copy array borrowing — the user keeps ownership.** The user passes in an array; the GPU works
   on it asynchronously; **we do not copy the array**. The GPU gets a *non-owning view/handle*, and
   the user **retains ownership** the whole time. (The GPU "has a copy" only in the sense of access —
   not a deep copy of the data.)
4. **The borrow is protected without us doing the threading.** A **simple mutex around the CPU↔GPU
   communication interface** guards every transfer/submission. While the GPU is reading from (or
   writing to) a CPU array, that array **cannot be taken, freed, disposed, moved, or mutated** out
   from under the GPU. The guard releases when the GPU operation completes. So: we don't thread, but
   the user's threading is made safe.
5. **This model lives in the `gpu` (easy) layer.** Because `gpu` is a slim interface over the native
   APIs, it owns the async + array-reuse/borrow ergonomics. The raw `gpu.vulkan` / `gpu.metal` layers
   stay faithful to their native APIs (including those APIs' own synchronization primitives), so a
   native engineer is never boxed in.

### Mechanism to implement (outline)

- A **GPU lease** (RAII borrow handle): wraps a user array (e.g. a cheatah `ndarray` — pointer +
  length + element type), pins it, and hands the backend a non-owning view. It holds the CPU↔GPU
  interface guard (and tracks the completion fence / timeline-semaphore value) until the GPU op
  finishes, then releases — automatically, via destruction. Ownership never transfers.
- The CPU↔GPU interface is the single choke point the mutex protects, so concurrent user threads
  can submit/transfer without racing and without the library imposing a thread model.
- Lifetime safety: the lease keeps the array alive/valid for exactly as long as the GPU needs it;
  attempting to free/resize a leased array is prevented (or diagnosed), never undefined.

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
- **Layering.** This 1:1 surface is the comfort layer for native Vulkan engineers; the ergonomic
  `import gpu` sits on top with the async + no-copy-borrow model. Raw Vulkan structs make a direct
  `.purr` 1:1 path awkward, so the cheatah-facing convenience lives in `import gpu`.
