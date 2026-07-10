#pragma once

/**
 * @file handles.hpp
 * @brief gpu.vulkan — conversion between native Vulkan handles and cheatah's `long long` tokens.
 *
 * The generated forwarders in `commands.hpp` already carry cheatah into Vulkan: every command has a
 * cheatah-friendly overload that takes plain cheatah numbers and casts them to the exact Vulkan
 * widths for you — `long long` to a handle, `long long` to `uint32_t`/`VkDeviceSize`, `double` to
 * `float`. So a caller holding tokens calls `CreateImage(device, &info, nullptr, &image)` with a
 * `long long` device and never writes a cast.
 *
 * Three places that conversion cannot reach, because they are not scalar command parameters:
 *
 *  1. **Out-parameters.** `CreateImage(..., VkImage* pImage)` hands back a NATIVE handle; storing it
 *     as a cheatah int needs the opposite direction.
 *  2. **Handle arrays.** `WaitForFences(..., const VkFence* pFences, ...)` takes a pointer to real
 *     `VkFence`s, not to tokens.
 *  3. **Struct fields.** `VkImageMemoryBarrier::image`, `VkSubmitInfo::pCommandBuffers` — the caller
 *     fills a native struct.
 *
 * @ref token and @ref handle are those two conversions, named once here so no consumer re-derives
 * the cast. They are the documented inverse of what the generated overloads do inline.
 *
 * Handle representation: a *dispatchable* handle (`VkDevice`, `VkQueue`, …) is always a pointer. A
 * *non-dispatchable* handle (`VkImage`, `VkFence`, …) is a pointer when `VK_USE_64_BIT_PTR_DEFINES`
 * is 1 (the default on 64-bit) and a plain `uint64_t` otherwise. Both are handled.
 *
 * This is a C++ shim utility: a `.purr` program never sees a native handle — it holds only tokens.
 */

#include <concepts>
#include <cstdint>
#include <type_traits>

#include "loader.hpp"

namespace cheatah::gpu::vulkan {

/**
 * A native Vulkan handle: a pointer (every dispatchable handle, and every non-dispatchable one when
 * `VK_USE_64_BIT_PTR_DEFINES` is 1) or a `uint64_t` (non-dispatchable handles otherwise). Constrains
 * @ref token and @ref handle so a mistyped argument fails at the call site, not inside a cast.
 */
template <class H>
concept Handle = std::is_pointer_v<H> || std::same_as<H, std::uint64_t>;

/**
 * The cheatah int token standing for a native Vulkan handle — what you store in a plain struct and
 * hand back to the generated `long long` overloads. `VK_NULL_HANDLE` maps to 0.
 * @param native the Vulkan handle to convert.
 * @return the handle as a cheatah int.
 * @complexity O(1).
 * @alloc none.
 * @test VulkanHandles.RoundTrip
 */
template <Handle H>
inline long long token(H native) noexcept {
    if constexpr (std::is_pointer_v<H>) {
        return static_cast<long long>(reinterpret_cast<std::uintptr_t>(native));
    } else {
        return static_cast<long long>(native);
    }
}

/**
 * The native Vulkan handle a cheatah int token stands for — for the out-params, handle arrays, and
 * native struct fields the generated overloads cannot take a token for. 0 maps to `VK_NULL_HANDLE`.
 * @tparam H the Vulkan handle type to recover (e.g. `VkImage`).
 * @param value the cheatah int token, as produced by @ref token.
 * @return the native handle.
 * @complexity O(1).
 * @alloc none.
 * @test VulkanHandles.RoundTrip
 */
template <Handle H>
inline H handle(long long value) noexcept {
    if constexpr (std::is_pointer_v<H>) {
        return reinterpret_cast<H>(static_cast<std::uintptr_t>(value));
    } else {
        return static_cast<H>(static_cast<std::uint64_t>(value));
    }
}

}  // namespace cheatah::gpu::vulkan
