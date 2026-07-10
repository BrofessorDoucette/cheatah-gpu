# cheatah-gpu

> **The simplest way onto the GPU from [cheatah](https://github.com/BrofessorDoucette/cheatah).**
> One shared interface over the native GPU APIs — **Vulkan** and **Metal** — so you can start doing
> things with your GPU, for *any* reason, without the usual bring-up pain.

**Version: `v0.4.0-prealpha`** &nbsp;·&nbsp; 🚧 early, pre-alpha work in progress.

The compute-dimensioning core and the compile-time backend-selection layer are working and fully
tested. The full Vulkan surface is generated (in cheatah) from the registry and committed under
`gpu/vulkan/` as source — its tests + 3-device coverage land in the next release. The native Metal
backend is on the roadmap. Source under `gpu/` is hand-authored or generated, header-only C++20.

## What it is

It exposes each native GPU API **as faithfully as the API itself**, and does exactly one thing on
top: it **fixes the typing**, so cheatah's numbers reach a C API that wants exact widths.

| `import …` | for | flavor |
|------------|-----|--------|
| **`gpu.vulkan`** | the Vulkan C API from cheatah | kept **as true to the native Vulkan C API as possible** |
| **`gpu.metal`** | the Metal API from cheatah | kept **as true to the native Metal API as possible** |
| **`gpu`** | the package header | the backend switch + dispatch math + the active backend's surface |

Every generated forwarder carries a cheatah-friendly overload: pass a `long long` where Vulkan wants
a handle or a `uint32_t`/`VkDeviceSize`, a `double` where it wants a `float` — the cast is done for
you. There is **no easy/simplified layer here, by design**: what "open a device, clear a target, read
it back" should mean is a policy decision (what is synchronous, who owns memory, what a frame is),
and it belongs to the consumer — a renderer or engine builds exactly the layer it wants on top of
these surfaces. cheatah-gpu stays the honest ground, and makes *setup* and *access* trivial:

```sh
biome add cheatah-gpu      # pulls the extension + provisions the GPU userspace stack
```

```purr
import gpu.dispatch as dispatch

# how many workgroups to cover 1,000,000 items at local_size_x = 256?
let groups = dispatch.group_count_1d(1000000, 256)        # 3907
let safe   = dispatch.clamp_group_count(groups, 65535)    # clamp to the device limit
```

Import convention: alias each submodule to its last segment — `import gpu.dispatch as dispatch`,
`import gpu.vulkan as vk`, `import gpu.metal as mtl` — while the package header stays `import gpu`.

Then write a [Slang](https://shader-slang.org/) shader (see [`shaders/hello.slang`](shaders/hello.slang))
and run it. Bringing up a **window** is intentionally *not* this library's job — that's
project-specific (some want GLFW, others SDL), so it lives in the consuming extension; cheatah-gpu
just hands it the surface/swapchain primitives to make it a breeze.

## Painless install

`biome add cheatah-gpu` runs [`scripts/install-deps.sh`](scripts/install-deps.sh), which installs the
**userspace GPU stack** (Vulkan loader + validation layers, Slang's `slangc`; GLFW for the tests) via
your platform's package manager (apt / dnf / pacman / brew). It does **not** force-install kernel GPU
drivers — those are machine-specific; [`scripts/doctor.sh`](scripts/doctor.sh) checks your setup and
tells you exactly what to do:

```sh
scripts/doctor.sh
#  ✓ Vulkan loader present
#  ✓ Vulkan device: NVIDIA GeForce RTX ...
#  ✓ slangc compiles shaders/hello.slang -> valid SPIR-V
#  cheatah-gpu: ready for GPU work.
```

The per-platform package lists live in [`cheatah.toml`](cheatah.toml) under `[system-dependencies]` —
the convention a future `biome install`/`biome doctor` will consume directly.

## Design

- **Vulkan C API, not C++ bindings** — avoids template/codegen bloat and tracks the spec's better
  docs. Built the modern way: the **[volk](https://github.com/zeux/volk)** meta-loader and
  **[VMA](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator)**, targeting Vulkan 1.3
  (dynamic rendering, buffer device address, descriptor indexing, synchronization2, timeline
  semaphores).
- **Compile-time backend selection — no runtime bloat.** The build (which knows the target) picks
  Vulkan *or* Metal at compile time via `gpu/backend.hpp`, so a delivered binary never carries code
  for the API it isn't using. No vtables, no both-API binary.
- **Static typing, concepts & templates**, the optional pattern (`std::optional`) for fallible
  lookups — the cheatah house style. The interface is resolved and constraint-checked at compile time.
- **No internal threading.** cheatah-gpu spawns no threads of its own — your threading model is
  yours, and the native surfaces stay true to their APIs. Synchronisation, async transfer, and
  ownership policy belong to the consumer that builds on top. See [`docs/DESIGN.md`](docs/DESIGN.md).
- **macOS prefers native Metal**; MoltenVK is a fallback only when the native Metal backend isn't
  available.
- **Zero dependencies for the core**; the GPU stack is provisioned by the build + install script.

## Modules

| module | what | status |
|--------|------|--------|
| [`gpu.backend`](gpu/backend.hpp) | compile-time backend selection + shared-interface conventions | **working** |
| [`gpu.dispatch`](gpu/dispatch/) | compute-shader dispatch-dimensioning math (GPU-native `uint32`) | **working** |
| [`gpu.vulkan`](gpu/vulkan/) | Vulkan backend (C API · volk · VMA · Slang · 1.3 features) | roadmap |
| [`gpu.metal`](gpu/metal/) | native Metal backend for Apple platforms | roadmap |

## Layout

```
gpu/        the package (import root): backend.hpp, dispatch/, vulkan/ (roadmap), metal/ (roadmap)
shaders/    Slang shaders (hello.slang is the smoke test the doctor compiles)
tests/      C++ unit tests (GoogleTest) — 100% coverage of the headers
systests/   cheatah (.purr) system tests — exercise `import gpu.*` end-to-end
scripts/    qa_gate.sh, coverage.sh, doc_coverage.sh, cppcheck.sh, install-deps.sh, doctor.sh
cmake/      CPM.cmake, Vulkan.cmake (provisions volk/VMA + the GPU stack)
```

## Coverage

<!-- coverage:start -->
| Metric | gpu package |
|--------|-------------|
| **Lines** | 100.00% (37/37) |
| **Functions** | 100.00% (11/11) |
| Regions | 100.00% |
| Branches | 100.00% |
<!-- coverage:end -->

## Tested on

The `gpu.vulkan` coverage matrix runs against three physical devices, spanning software, integrated,
and discrete GPUs across the two major Linux Vulkan drivers:

| device | type | driver |
|--------|------|--------|
| Mesa **llvmpipe** (lavapipe) | software (CPU) | Mesa |
| **Intel Iris Xe** Graphics (ADL GT2) | integrated | Intel open-source Mesa |
| **NVIDIA GeForce RTX 3070 Ti** Laptop | discrete | NVIDIA proprietary |

Each Vulkan function is exercised on every device that supports it (the coverage denominator is
per-device — a function the hardware doesn't support is excluded and logged, never silently skipped).

## Developing

Needs the sibling cheatah toolchain at `../cheatah` (override with `-DCHEATAH_DIR=…`). One-time:

```sh
./scripts/setup-hooks.sh          # pre-push runs the QA gate
```

The QA gate (`scripts/qa_gate.sh`, also the pre-push hook) is the bar for every push and hard-fails
unless **all** pass:

1. **100% unit-test coverage** (clang source-based, lines + functions over `gpu/**/*.hpp`)
2. **100% Javadoc** on the public C++ API (strict Doxygen)
3. **cheatah `.purr` system tests** all print `RESULT: PASS`
4. unit tests under **ASan + UBSan**, then **Valgrind** memcheck
5. **cppcheck** (performance + security) clean

```sh
cmake --preset debug && ctest --preset debug   # build + run everything
bash scripts/qa_gate.sh                          # the full gate
```

Every public function documents `@param`, `@return`, `@complexity`, `@alloc`, and a `@test`
(unit) / `@systest` (cheatah) — enforced by the gate.

## License

MIT — © 2026 BigBrain LLC (Joshua Doucette, on its behalf). See [LICENSE](LICENSE) and [NOTICE](NOTICE).
