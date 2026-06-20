#pragma once

/**
 * @file backend.hpp
 * @brief gpu.backend — compile-time selection of the GPU backend (Vulkan or Metal).
 *
 * cheatah-gpu exposes ONE shared interface over two native APIs. Two house rules keep that
 * interface from ever limiting a modern feature of either API, and keep delivered binaries lean:
 *
 *  1. **Compile-time backend selection — never runtime polymorphism.** The build (driven by biome,
 *     which knows the target device/platform) defines exactly one of `CHEATAH_GPU_BACKEND_VULKAN` /
 *     `CHEATAH_GPU_BACKEND_METAL`. Everything downstream branches with `#if` / `if constexpr` on
 *     @ref cheatah::gpu::active_backend, so only the active backend's code is compiled and linked —
 *     no vtables, no both-API binary, no bloat for an API this build doesn't use.
 *  2. **Static typing, concepts, and templates** (the cheatah standard): the interface is resolved
 *     at compile time and constraint-checked with concepts, and fallible lookups use the **optional
 *     pattern** (`std::optional`) rather than sentinels or exceptions — see @ref
 *     cheatah::gpu::backend_from_name.
 *
 * This header is the working seed of that pattern; the real Vulkan/Metal surfaces (roadmap) plug in
 * behind the same compile-time switch.
 */

#include <concepts>
#include <cstdint>
#include <optional>
#include <string_view>

// Default selection when the build hasn't forced one: native Metal on Apple, Vulkan everywhere else.
// (cheatah-gpu prefers native Metal on Apple; the MoltenVK Vulkan fallback is wired by the build,
// see cmake/Vulkan.cmake.) A build pins the choice by defining exactly one macro.
#if !defined(CHEATAH_GPU_BACKEND_VULKAN) && !defined(CHEATAH_GPU_BACKEND_METAL)
#  ifdef __APPLE__
#    define CHEATAH_GPU_BACKEND_METAL 1
#  else
#    define CHEATAH_GPU_BACKEND_VULKAN 1
#  endif
#endif

namespace cheatah::gpu {

/// The native GPU API a build targets. Exactly one is active per binary, chosen at compile time.
enum class Backend : std::uint8_t {
    vulkan,  ///< The Vulkan backend (Linux, Windows, Android; Apple via the MoltenVK fallback).
    metal,   ///< The native Metal backend (Apple platforms).
};

/// The backend selected for THIS binary at compile time (see the `CHEATAH_GPU_BACKEND_*` macros).
inline constexpr Backend active_backend =
#ifdef CHEATAH_GPU_BACKEND_METAL
    Backend::metal;
#else
    Backend::vulkan;
#endif

/// Constrains a template parameter to the GPU @ref Backend enum — the concept-checked, static style
/// the rest of the interface follows.
template <class T>
concept BackendKind = std::same_as<T, Backend>;

/**
 * The lowercase name of a backend — for logs, a `doctor` report, or diagnostics.
 * @param backend the backend to name.
 * @return "metal" for @ref Backend::metal, otherwise "vulkan".
 * @complexity O(1).
 * @alloc none.
 * @test Backend.Name
 */
constexpr std::string_view backend_name(Backend backend) {
    return backend == Backend::metal ? "metal" : "vulkan";
}

/**
 * The name of the backend this binary was compiled for.
 * @return the active backend's name (see @ref active_backend).
 * @complexity O(1).
 * @alloc none.
 * @test Backend.Active
 */
constexpr std::string_view active_backend_name() { return backend_name(active_backend); }

/**
 * Parse a backend name — the optional pattern cheatah-gpu uses for fallible lookups: a missing value
 * is an empty optional, never an exception or a sentinel.
 * @param name a backend name, "vulkan" or "metal" (case-sensitive).
 * @return the matching @ref Backend, or `std::nullopt` when @p name is neither.
 * @complexity O(1).
 * @alloc none.
 * @test Backend.FromName
 */
constexpr std::optional<Backend> backend_from_name(std::string_view name) {
    if (name == "vulkan") { return Backend::vulkan; }
    if (name == "metal") { return Backend::metal; }
    return std::nullopt;
}

/**
 * Whether @p backend is the one this binary was compiled for — a concept-constrained, compile-time
 * comparison (the real interface gates backend-specific code on exactly this).
 * @param backend the backend to test.
 * @return true iff @p backend equals @ref active_backend.
 * @complexity O(1).
 * @alloc none.
 * @test Backend.IsActive
 */
template <BackendKind B>
constexpr bool is_active(B backend) {
    return backend == active_backend;
}

}  // namespace cheatah::gpu
