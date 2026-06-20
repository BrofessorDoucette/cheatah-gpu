#pragma once

/**
 * @file dispatch.hpp
 * @brief gpu.dispatch — compute-shader dispatch-dimensioning math.
 *
 * SCAFFOLD / OUTLINE. This is the seed module for cheatah-gpu: the small, backend-agnostic
 * core every GPU compute backend needs to turn a problem size into a workgroup launch. It is
 * pure C++20 integer arithmetic — header-only, allocation-free, zero dependencies, and no
 * platform headers — so it builds, tests, and documents to 100% on a machine with no GPU. The
 * real backends (gpu.vulkan, gpu.metal) are roadmap; they will feed these counts straight into
 * `vkCmdDispatch` / `MTLComputeCommandEncoder`.
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
 * @systest DispatchSys.CeilDiv
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
 * @systest DispatchSys.GroupCount1d
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
 * @systest DispatchSys.ClampGroupCount
 */
inline constexpr std::uint32_t clamp_group_count(std::uint32_t want, std::uint32_t device_max) {
    return want < device_max ? want : device_max;
}

}  // namespace cheatah::gpu::dispatch
