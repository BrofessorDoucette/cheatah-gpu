#pragma once

/**
 * @file handles.hpp
 * @brief gpu.metal — conversion between native Metal objects and cheatah's `long long` tokens.
 *
 * The Metal mirror of `gpu/vulkan/handles.hpp`, so a shim written against either backend spells the
 * conversion the same way. Every Metal object (`MTL::Device`, `MTL::Buffer`, `MTL::Texture`, …) is
 * reached through a pointer, so a token is just that pointer widened to a cheatah int.
 *
 * A caller stores tokens in plain structs and converts back only where a native pointer is demanded
 * — a method receiver, an out-parameter, or a field of a native descriptor. `nullptr` maps to 0.
 *
 * This is a C++ shim utility: a `.purr` program never sees a native object — it holds only tokens.
 */

#include <concepts>
#include <cstdint>
#include <type_traits>

#include "metal.hpp"

namespace cheatah::gpu::metal {

/**
 * A native Metal object handle — always a pointer to an `MTL::`/`NS::` object. Constrains @ref token
 * and @ref handle so a mistyped argument fails at the call site, not inside a cast.
 */
template <class T>
concept Handle = std::is_pointer_v<T>;

/**
 * The cheatah int token standing for a native Metal object. `nullptr` maps to 0.
 * @param native the Metal object pointer to convert.
 * @return the object as a cheatah int.
 * @complexity O(1).
 * @alloc none.
 * @test MetalHandles.RoundTrip
 */
template <Handle T>
inline long long token(T native) noexcept {
    return static_cast<long long>(reinterpret_cast<std::uintptr_t>(native));
}

/**
 * The native Metal object a cheatah int token stands for. 0 maps to `nullptr`.
 * @tparam T the Metal object pointer type to recover (e.g. `MTL::Texture*`).
 * @param value the cheatah int token, as produced by @ref token.
 * @return the native object pointer.
 * @complexity O(1).
 * @alloc none.
 * @test MetalHandles.RoundTrip
 */
template <Handle T>
inline T handle(long long value) noexcept {
    return reinterpret_cast<T>(static_cast<std::uintptr_t>(value));
}

}  // namespace cheatah::gpu::metal
