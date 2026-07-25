# Changelog

All notable changes to cheatah-gpu. This project is **alpha** — expect breaking changes between
releases.

## v0.5.0-alpha (2026-07-25) — the native Metal backend ships, and cheatah-gpu graduates to alpha

Both native surfaces are now shipped and release-gated: the generated Vulkan surface (with the
portable WSI extensions) and a **native Metal backend** that runs on real Apple Silicon in CI and on
a software-emulated Metal device everywhere else. With that, cheatah-gpu graduates
**prealpha → alpha** and joins **Biome Standard 0.1.0-alpha** alongside cheatah v1.7.0-alpha.

### gpu.metal — the native Metal backend
- **The native Metal backend ships.** `import gpu.metal as mtl` exposes the Metal API the same way
  the Vulkan surface exposes Vulkan — 193 classes, 114 enums, 882 constants generated into
  `gpu/metal/types.hpp` (plus the separate Metal 4 surface), with `gpu/metal/handles.hpp` naming
  the native-object ⇄ cheatah-`long long` token boundary. Built by CMake (`cmake/Metal.cmake`) —
  ON by default on Apple — and gated by `scripts/metal_gate.sh` plus the
  `mtl:compute` / `mtl:multiline` / `mtl:texture` ctests.
- **metal-cpp is vendored** at `third_party/metal-cpp` — Apple's own MIT-licensed C++ Metal
  interface, ~39k lines across 136 headers, committed in-repo so the backend builds with **zero
  configuration and zero downloads** (the gate's "NO third-party download" policy).
- **The software-emulated Metal device grew a graphics half**: depth-tested, textured, indexed
  draws — texture → render-pass clear/draw → readback — so the full compute+graphics flow runs
  bit-correct on the CPU off Apple, sanitized (ASan+UBSan) and Valgrind/Helgrind-clean.
- **macOS CI on real Apple Silicon** (`.github/workflows/macos-metal.yml`, macos-14): the same
  tests that run on the emulator run on an actual Apple GPU on every push.

### gpu.vulkan — presentation, portably
- **The generated surface now emits the portable WSI extensions** — `VK_KHR_surface` and
  `VK_KHR_swapchain`, each under its own `#ifdef` guard — so a consumer can turn a rendered image
  into a visible frame; the platform-specific surface extensions stay out by design (windowing
  belongs to the consumer).
- **`gpu/vulkan/handles.hpp`** names the handle ⇄ cheatah-int conversion for the three places a
  token cannot go directly: out-params, handle arrays, and native struct fields — mirrored by
  `gpu/metal/handles.hpp` on the Metal side.

### gpu.dispatch — 3-D extents
- **3-D dispatch extents ship**: `Dim3` (unused axes default to 1 — one slice, not zero),
  `group_count_3d` (overflow-safe `ceil_div` per axis), and the `Dim3` overload of
  `clamp_group_count` against `maxComputeWorkGroupCount[3]`, feeding `vkCmdDispatch` and Metal's
  `MTLSize` alike.

### Tests — the compile-run tier arrives
- **Per-function `.purr` compile-run tests** for the documented core surface (previously zero, vs
  294 in the stdlib): eight `systests/test_*_cr_*.purr` programs covering `ceil_div` (including
  the `UINT32_MAX` overflow-safety proof), `group_count_1d`, both `clamp_group_count` overloads,
  `Dim3` + its per-axis `operator==`, `group_count_3d`, the `gpu.backend` query surface, and the
  backend-selection notice machinery. Each is discovered by the QA gate's stage-4 glob, compiles
  via purrc against the imported library, and must print `RESULT: PASS`. Every function's doc
  comment now carries a `@crtest` tag naming its test.
- **Tag audit of the doc-gated surface**: every `@complexity`/`@alloc` claim verified transitively
  against the implementation; dead `@systest` references (names that existed nowhere) now point at
  the real test files.

### Docs — the prose catches up with the code
- **Stale "roadmap/outline" prose corrected everywhere**: `gpu/vulkan/README.md` claimed "no
  compiled headers live here yet" while `commands.hpp` carries the full generated surface;
  `dispatch.hpp` still called itself a scaffold; `gpu/metal/README.md` said macOS runs MoltenVK
  "until this lands"; `docs/DESIGN.md` called `gpu/` an outline. All now tell the truth,
  losslessly.

### Release process — gates close both generated surfaces
- **`release.sh` now runs the Metal gate** and drift-checks the Metal generator (regenerate, fail
  if `gpu/metal/types.hpp`/`symbols.json` drifted), mirroring the Vulkan check — a release can no
  longer ship either generated surface unchecked. The GitHub release body comes from
  `RELEASE_NOTES.md` (required), the tag must match the README version line, and a git-archive
  source tarball is attached to the release.
- **`tools/metal-gen/regenerate.sh` no longer downloads metal-cpp** from a mirror — it uses the
  vendored `third_party/metal-cpp`, honoring the same no-download policy as the gate.
- **The `mtl:*` ctests run off-Apple too**: the debug/asan presets enable
  `CHEATAH_GPU_BUILD_METAL` on every platform (the emulated device backs it off Apple).

### Housekeeping
- **Biome Standard 0.1.0-alpha membership**: `cheatah.toml` pins the cheatah toolchain at
  **v1.7.0-alpha** (this release is tested against the toolchain cut in the same wave).
- **LICENSE**: copyright is held by **BigBrain LLC** (Joshua Doucette, on its behalf).

## v0.4.0-prealpha and earlier

Pre-changelog releases: the compute-dimensioning seed (`gpu.dispatch`), the compile-time backend
switch (`gpu.backend`), the generated Vulkan surface + volk loader + the 3-device Vulkan gate, the
`vk.<Name>` cheatah interface, and the QA/coverage/doc gates. See the git tags
`v0.1.0-prealpha` … `v0.4.0-prealpha`.
