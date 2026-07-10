# gpu.metal — native Metal backend for Apple platforms  ·  status: **roadmap**

`import gpu.metal` is the Apple **native interface**: a native **Metal** backend kept **as true
to the Metal API as possible**, so Apple users get first-class performance and Metal-only features
instead of a translation layer. There is no "easy" layer above it — cheatah-gpu's one value-add is
the **typing fix**, so a cheatah program passes plain numbers and object tokens and writes no casts
(see [`handles.hpp`](handles.hpp)). Thin bindings over the Metal C/Objective-C API; Slang authoring
→ Metal via Slang's Metal target.

> Outline only — no compiled headers yet (keeps the QA gate scoped to the tested seed).

## Why native Metal first on Apple
On Apple platforms the native Metal path is preferred over running Vulkan through **MoltenVK**:
lower overhead, full access to Metal-only features, and no translation surprises. The build reflects
this precedence: [`../../cmake/Vulkan.cmake`](../../cmake/Vulkan.cmake) only falls back to **MoltenVK**
when this backend is unavailable or not yet working (`CHEATAH_GPU_METAL_OK` is false). Until this
lands, macOS runs Vulkan via MoltenVK so nothing is blocked.
