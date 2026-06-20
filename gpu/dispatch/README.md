# gpu.dispatch — compute-shader dispatch-dimensioning  ·  status: **working**

The backend-agnostic core every GPU compute path needs: turn a problem size into a workgroup
launch. Pure C++20 integer arithmetic — header-only, allocation-free, **zero dependencies, no
platform headers** — so it builds, tests, and documents to 100% on a machine with no GPU. It is the
CPU-side math the Vulkan backend (roadmap, see [`../vulkan`](../vulkan)) feeds straight into
`vkCmdDispatch`.

```purr
import gpu.dispatch as dispatch

# cover 1,000,000 items at local_size_x = 256
let groups = dispatch.group_count_1d(1000000, 256)        # 3907
let safe   = dispatch.clamp_group_count(groups, 65535)    # clamp to maxComputeWorkGroupCount[0]
```

## Why `uint32_t` everywhere
Workgroup counts and local sizes are **32-bit unsigned on the hardware**: `vkCmdDispatch(uint32_t,
uint32_t, uint32_t)` and `VkPhysicalDeviceLimits::maxComputeWorkGroupCount[3]` are all `uint32_t`.
We never widen to 64-bit for dimensioning — it doesn't map to the dispatch ABI and wastes shader
registers.

## API
| function | meaning |
|----------|---------|
| `ceil_div(numerator, denom)` | groups to cover `numerator` items at `denom`/group (overflow-safe; `denom==0 → 0`) |
| `group_count_1d(items, local_size)` | the `groupCountX` for a 1-D dispatch |
| `clamp_group_count(want, device_max)` | clamp one axis to the device's workgroup-count limit |

3-D extents (`Dim3`, `group_count_3d`, per-axis clamp) land with the Vulkan backend, where the GPU
struct binding is defined.

## Tests
- **Unit** (C++ GoogleTest): [`../../tests/dispatch_test.cpp`](../../tests/dispatch_test.cpp) — drives 100% line + function coverage.
- **System** (cheatah `.purr`): [`../../systests/test_dispatch.purr`](../../systests/test_dispatch.purr), [`test_dispatch_limits.purr`](../../systests/test_dispatch_limits.purr) — exercise `import gpu.dispatch` end-to-end.

Every public function documents `@param`, `@return`, `@complexity`, `@alloc`, and a `@test` /
`@systest`; the QA gate enforces 100% Javadoc + 100% coverage.
