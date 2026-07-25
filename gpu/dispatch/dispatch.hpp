#pragma once

/**
 * @file dispatch.hpp
 * @brief gpu.dispatch — compute-shader dispatch-dimensioning math.
 *
 * This is the seed module of cheatah-gpu: the small, backend-agnostic core every GPU compute
 * backend needs to turn a problem size into a workgroup launch. It is pure C++20 integer
 * arithmetic — header-only, allocation-free, zero dependencies, and no platform headers — so it
 * builds, tests, and documents to 100% on a machine with no GPU. Both shipped backends consume
 * these counts: the generated Vulkan surface feeds them straight into `vkCmdDispatch`, and the
 * native Metal backend into `MTLComputeCommandEncoder` dispatches.
 *
 * Types are deliberately GPU-native: workgroup counts and local sizes are `std::uint32_t`,
 * because that is exactly what the hardware dispatch interface uses — `vkCmdDispatch(uint32_t,
 * uint32_t, uint32_t)` and `VkPhysicalDeviceLimits::maxComputeWorkGroupCount[3]` are all 32-bit
 * unsigned. We do NOT use 64-bit integers for dimensioning: they don't map to the dispatch ABI
 * and waste registers in the shader. `import gpu.dispatch` resolves this header.
 */

#include <cstdint>

namespace cheatah::gpu::dispatch {

/**
 * Number of workgroups needed to cover @p numerator items at @p denom items per group
 * (ceiling division). Computed overflow-safe — without the usual `(n + d - 1) / d`, which
 * overflows for @p numerator near `UINT32_MAX`.
 * @param numerator total items to process (e.g. elements in a buffer).
 * @param denom items handled per workgroup — the shader's local size on this axis.
 * @return ceil(numerator / denom); 0 when @p denom is 0 (an empty/invalid dispatch).
 * @complexity O(1).
 * @alloc none.
 * @test Dispatch.CeilDiv
 * @crtest systests/test_dispatch_cr_ceil_div.purr
 * @systest systests/test_dispatch.purr
 */
inline constexpr std::uint32_t ceil_div(std::uint32_t numerator, std::uint32_t denom) {
    if (denom == 0u) { return 0u; }
    return numerator / denom + (numerator % denom != 0u ? 1u : 0u);
}

/**
 * One-dimensional workgroup count for a 1-D compute dispatch: the `groupCountX` you pass to
 * `vkCmdDispatch` so @p items invocations are covered at @p local_size threads per group.
 * @param items total invocations the shader must cover.
 * @param local_size the shader's `local_size_x` (threads per workgroup).
 * @return the number of workgroups to dispatch on X; 0 when @p local_size is 0.
 * @complexity O(1).
 * @alloc none.
 * @test Dispatch.GroupCount1d
 * @crtest systests/test_dispatch_cr_group_count_1d.purr
 * @systest systests/test_dispatch.purr
 * @systest systests/test_dispatch_limits.purr
 */
inline constexpr std::uint32_t group_count_1d(std::uint32_t items, std::uint32_t local_size) {
    return ceil_div(items, local_size);
}

/**
 * Clamp a desired workgroup count for one axis to the device's limit, so a dispatch never
 * exceeds `VkPhysicalDeviceLimits::maxComputeWorkGroupCount[axis]`.
 * @param want the workgroup count the problem size asks for.
 * @param device_max the device's maximum workgroup count on this axis.
 * @return @p want when it fits, otherwise @p device_max.
 * @complexity O(1).
 * @alloc none.
 * @test Dispatch.ClampGroupCount

 * @crtest systests/test_dispatch_cr_clamp_group_count.purr
 * @systest systests/test_dispatch_limits.purr
 */
inline constexpr std::uint32_t clamp_group_count(std::uint32_t want, std::uint32_t device_max) {
    return want < device_max ? want : device_max;
}

/**
 * Three-dimensional dispatch extents — the (x, y, z) triple every GPU dispatch interface
 * takes, whether it is `vkCmdDispatch(x, y, z)` or Metal's `MTLSize`. Also spells problem
 * sizes and per-axis device limits (`maxComputeWorkGroupCount[3]`), so one type serves items,
 * local sizes, workgroup counts, and clamps. Unused axes default to 1 — a 1-D dispatch is
 * `Dim3{items}` — matching how the hardware treats a missing axis (one slice, not zero).
 * @test Dispatch.Dim3Defaults
 * @crtest systests/test_dispatch_cr_dim3.purr
 * @systest systests/test_dispatch.purr
 */
struct Dim3 {
    std::uint32_t x = 1u;  ///< extent on X — the only axis a 1-D dispatch sizes.
    std::uint32_t y = 1u;  ///< extent on Y; 1 when unused.
    std::uint32_t z = 1u;  ///< extent on Z; 1 when unused.
};

/**
 * Per-axis equality of two extents (all of x, y, and z match).
 * @param a,b the extents to compare.
 * @return true when every axis is equal.
 * @complexity O(1).
 * @alloc none.
 * @test Dispatch.Dim3Equality
 * @crtest systests/test_dispatch_cr_dim3.purr
 */
inline constexpr bool operator==(Dim3 a, Dim3 b) {
    return a.x == b.x && a.y == b.y && a.z == b.z;
}

/**
 * Three-dimensional workgroup count for a compute dispatch: the (groupCountX, groupCountY,
 * groupCountZ) you pass to `vkCmdDispatch` so an @p items volume is covered at @p local_size
 * threads per group on each axis — @ref ceil_div applied per axis, with its overflow safety
 * and `0`-on-zero-denominator semantics.
 * @param items total invocations to cover on each axis (e.g. an image's width × height).
 * @param local_size the shader's (local_size_x, local_size_y, local_size_z).
 * @return the per-axis workgroup counts; an axis is 0 when its @p local_size axis is 0.
 * @complexity O(1).
 * @alloc none.
 * @test Dispatch.GroupCount3d
 * @crtest systests/test_dispatch_cr_group_count_3d.purr
 * @systest systests/test_dispatch.purr
 */
inline constexpr Dim3 group_count_3d(Dim3 items, Dim3 local_size) {
    return Dim3{ceil_div(items.x, local_size.x), ceil_div(items.y, local_size.y),
                ceil_div(items.z, local_size.z)};
}

/**
 * Clamp a desired workgroup count to the device's per-axis limits, so a 3-D dispatch never
 * exceeds `VkPhysicalDeviceLimits::maxComputeWorkGroupCount[3]` on any axis — the scalar
 * @ref clamp_group_count applied per axis.
 * @param want the workgroup counts the problem size asks for.
 * @param device_max the device's maximum workgroup count per axis.
 * @return @p want with each axis clamped to its @p device_max axis.
 * @complexity O(1).
 * @alloc none.
 * @test Dispatch.ClampGroupCount3d
 * @crtest systests/test_dispatch_cr_clamp_group_count_3d.purr
 * @systest systests/test_dispatch.purr
 */
inline constexpr Dim3 clamp_group_count(Dim3 want, Dim3 device_max) {
    return Dim3{clamp_group_count(want.x, device_max.x), clamp_group_count(want.y, device_max.y),
                clamp_group_count(want.z, device_max.z)};
}

}  // namespace cheatah::gpu::dispatch
