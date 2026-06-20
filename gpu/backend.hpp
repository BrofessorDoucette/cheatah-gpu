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
#include <cstdio>
#include <cstdlib>
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

/// @def CHEATAH_GPU_METAL_AVAILABLE
/// 1 when native Metal can run on this platform (Apple), 0 otherwise. Vulkan is assumed available
/// everywhere (on Apple via MoltenVK).
#if defined(__APPLE__)
#  define CHEATAH_GPU_METAL_AVAILABLE 1
#else
#  define CHEATAH_GPU_METAL_AVAILABLE 0
#endif

/// @def CHEATAH_GPU_REQUESTED_METAL
/// 1 if the build requested Metal (explicitly or via the platform default), recorded before any
/// auto-correction switches the active backend.
#if defined(CHEATAH_GPU_BACKEND_METAL)
#  define CHEATAH_GPU_REQUESTED_METAL 1
#else
#  define CHEATAH_GPU_REQUESTED_METAL 0
#endif

// Auto-correction, never silent. cheatah is performance-first: an inoptimal API picked behind the
// user's back is a bug. A build that asks for a backend the platform can't run is SWITCHED to the one
// it can; a build that asks for a runnable-but-suboptimal backend is flagged. Both raise a
// compile-time #warning here AND a runtime notice (warn_backend_selection, below). Define
// CHEATAH_GPU_NO_BACKEND_WARNING to silence the runtime notice in production.
#if defined(CHEATAH_GPU_BACKEND_METAL) && !CHEATAH_GPU_METAL_AVAILABLE
#  warning "cheatah-gpu: Metal was requested (CHEATAH_GPU_BACKEND_METAL) but Metal only runs on Apple platforms. Switching this build to the Vulkan backend. Define CHEATAH_GPU_BACKEND_VULKAN to select it explicitly."
#  undef CHEATAH_GPU_BACKEND_METAL
#  ifndef CHEATAH_GPU_BACKEND_VULKAN
#    define CHEATAH_GPU_BACKEND_VULKAN 1
#  endif
#  define CHEATAH_GPU_BACKEND_SWITCHED 1
#endif

#if defined(CHEATAH_GPU_BACKEND_VULKAN) && CHEATAH_GPU_METAL_AVAILABLE && !defined(CHEATAH_GPU_BACKEND_SWITCHED)
#  warning "cheatah-gpu: using Vulkan on an Apple platform (via MoltenVK) is SUBOPTIMAL; native Metal is preferred. Define CHEATAH_GPU_BACKEND_METAL for best performance."
#  define CHEATAH_GPU_BACKEND_SUBOPTIMAL 1
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

/// Whether native Metal can run on this platform (Apple only). Vulkan is assumed available everywhere.
inline constexpr bool metal_available = CHEATAH_GPU_METAL_AVAILABLE;

/// The backend the build REQUESTED (the platform default counts as a request), before auto-correction.
inline constexpr Backend requested_backend = CHEATAH_GPU_REQUESTED_METAL ? Backend::metal : Backend::vulkan;

/// True iff the requested backend could not run here and was auto-switched to @ref active_backend.
inline constexpr bool backend_was_switched =
#ifdef CHEATAH_GPU_BACKEND_SWITCHED
    true;
#else
    false;
#endif

/// True iff the active backend runs here but is not the platform's preferred (e.g. Vulkan on Apple).
inline constexpr bool backend_is_suboptimal =
#ifdef CHEATAH_GPU_BACKEND_SUBOPTIMAL
    true;
#else
    false;
#endif

/**
 * One-shot guard so the runtime backend notice is printed at most once per process.
 * @return a reference to the process-wide "already printed" flag.
 * @complexity O(1).
 * @alloc none.
 */
inline bool& backend_warning_emitted() {
    static bool emitted = false;
    return emitted;
}

/**
 * User-controlled mute. Once true, @ref warn_backend_selection stays quiet.
 * @return a reference to the process-wide "silenced" flag.
 * @complexity O(1).
 * @alloc none.
 */
inline bool& backend_warning_silenced() {
    static bool silenced = false;
    return silenced;
}

/**
 * Silence the runtime backend notice from here on. The first notice is deliberately loud, but using
 * (say) Vulkan on macOS on purpose is perfectly fine — call this, or set the environment variable
 * `CHEATAH_GPU_SILENCE_BACKEND_WARNING`, or build with `-DCHEATAH_GPU_NO_BACKEND_WARNING`, to make it
 * stop. This is a CHOICE the user owns, not a forced nag.
 * @param silence true to mute (the default), false to re-enable.
 * @complexity O(1).
 * @alloc none.
 * @test Backend.Silence
 */
inline void silence_backend_warning(bool silence = true) { backend_warning_silenced() = silence; }

/**
 * Print the runtime backend notice — at most once — if the build was auto-switched or is using a
 * suboptimal backend. Never silent by default: a suboptimal GPU API chosen behind the user's back is
 * a performance bug. But the notice is dismissable — it is suppressed if @ref silence_backend_warning
 * was called, if `CHEATAH_GPU_SILENCE_BACKEND_WARNING` is set in the environment, or if built with
 * `CHEATAH_GPU_NO_BACKEND_WARNING` — and the message itself says how. No-op when the selection is
 * optimal.
 * @param out where to write the notice (defaults to stderr).
 * @return true iff a notice was printed by this call.
 * @complexity O(1).
 * @alloc none.
 * @test Backend.WarnSwitched
 * @systest systests/backend/test_backend_switch.purr
 */
inline bool warn_backend_selection(std::FILE* out = stderr) {
#if !defined(CHEATAH_GPU_NO_BACKEND_WARNING)
    if constexpr (backend_was_switched || backend_is_suboptimal) {
        if (backend_warning_emitted() || backend_warning_silenced()) { return false; }
        if (std::getenv("CHEATAH_GPU_SILENCE_BACKEND_WARNING") != nullptr) {
            backend_warning_silenced() = true;
            return false;
        }
        backend_warning_emitted() = true;
        const char* how = "  (This is fine if intentional. Silence it: set "
                          "CHEATAH_GPU_SILENCE_BACKEND_WARNING=1, call "
                          "cheatah::gpu::silence_backend_warning(), or build -DCHEATAH_GPU_NO_BACKEND_WARNING.)";
        if constexpr (backend_was_switched) {
            std::fprintf(out,
                "cheatah-gpu WARNING: backend '%s' was requested but cannot run on this platform; "
                "using '%s' instead.\n%s\n",
                backend_name(requested_backend).data(), active_backend_name().data(), how);
        } else {
            std::fprintf(out,
                "cheatah-gpu WARNING: backend '%s' runs here but is SUBOPTIMAL on this platform; "
                "'metal' is preferred for performance.\n%s\n",
                active_backend_name().data(), how);
        }
        return true;
    }
#endif
    (void)out;
    return false;
}

}  // namespace cheatah::gpu
