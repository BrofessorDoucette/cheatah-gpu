# cheatah-gpu v0.5.0-alpha — the native Metal backend ships, and cheatah-gpu graduates to alpha

cheatah-gpu is now a member of **Biome Standard 0.1.0-alpha**, released alongside cheatah
**v1.7.0-alpha** (the toolchain this release is tested against). Both native surfaces are shipped
and release-gated, and the project graduates **prealpha → alpha**.

## Highlights

- **Native Metal backend** (`import gpu.metal as mtl`): the Metal API generated 1:1 into the
  cheatah interface (193 classes, 114 enums, 882 constants, plus the Metal 4 surface), with the
  token boundary named in `gpu/metal/handles.hpp`. Apple's metal-cpp is **vendored** (~39k lines,
  zero downloads). Gated by `scripts/metal_gate.sh` + the `mtl:compute`/`mtl:multiline`/
  `mtl:texture` ctests — on a **real Apple Silicon GPU in CI** (macos-14), and on the
  software-emulated Metal device everywhere else, whose new **graphics half** does depth-tested,
  textured, indexed draws bit-correct on the CPU.
- **Vulkan presentation, portably**: the generated surface now emits `VK_KHR_surface` +
  `VK_KHR_swapchain` (platform surface extensions stay out by design), and
  `gpu/vulkan/handles.hpp` names the handle ⇄ cheatah-int conversion.
- **3-D dispatch extents** in `gpu.dispatch`: `Dim3`, `group_count_3d`, and per-axis clamping
  against `maxComputeWorkGroupCount[3]`.
- **A new compile-run test tier**: eight per-function `.purr` programs (tagged `@crtest` in the
  headers) compile against the imported library and prove the documented contracts — including
  the `UINT32_MAX` overflow-safety of `ceil_div` — from cheatah itself.
- **Truthful docs**: all stale "roadmap/outline" prose corrected; a tag audit verified every
  `@complexity`/`@alloc` claim transitively and repointed dead test references.
- **A stricter release path**: `release.sh` now regenerates + drift-checks BOTH generated
  surfaces (Vulkan and Metal), runs all three gates (host, Metal, Vulkan), takes its notes from
  `RELEASE_NOTES.md`, refuses a tag the README version line doesn't name, and attaches a source
  tarball. The `mtl:*` ctests now run off-Apple too.
- **License**: copyright held by BigBrain LLC (Joshua Doucette, on its behalf).

Full details in [CHANGELOG.md](CHANGELOG.md).
