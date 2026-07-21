# gpu.metal — native Metal backend for Apple platforms  ·  status: **shipped**

`import gpu.metal` is the Apple **native interface**: a native **Metal** backend kept **as true
to the Metal API as possible**, so Apple users get first-class performance and Metal-only features
instead of a translation layer. There is no "easy" layer above it — cheatah-gpu's one value-add is
the **typing fix**, so a cheatah program passes plain numbers and object tokens and writes no casts
(see [`handles.hpp`](handles.hpp)). Thin bindings over the Metal C/Objective-C API; Slang authoring
→ Metal via Slang's Metal target.

The backend builds and runs under CMake via `-DCHEATAH_GPU_BUILD_METAL=ON` (ON by default on Apple):
`cmake/Metal.cmake` uses the vendored Apple metal-cpp in [`../../third_party/metal-cpp`](../../third_party/metal-cpp)
(no download), and the `mtl:compute` / `mtl:multiline` / `mtl:texture` ctests run on the GPU on Apple
and on the software emulator ([`emulated/`](emulated)) off Apple. `scripts/metal_gate.sh` runs the same
suite.

## Why native Metal first on Apple
On Apple platforms the native Metal path is preferred over running Vulkan through **MoltenVK**:
lower overhead, full access to Metal-only features, and no translation surprises. The build reflects
this precedence: [`../../cmake/Vulkan.cmake`](../../cmake/Vulkan.cmake) only falls back to **MoltenVK**
when this backend is unavailable or not yet working (`CHEATAH_GPU_METAL_OK` is false). Until this
lands, macOS runs Vulkan via MoltenVK so nothing is blocked.
