#pragma once

// window — an OPTIONAL test fixture exposed as a NORMAL cheatah module. tests/window/test_window.purr
// does `import window` and calls window.open / window.draw / window.close directly — no cpp{} escape
// hatches, no extern "C", no casts. The window + the whole Vulkan render setup live in window.cpp
// (built into the co-located libcheatah_window.a that purrc links); cheatah just calls these.
//
// Windowing is NOT part of the shipped gpu library (a consumer concern) — this lives under tests/ and
// is built only by scripts/window_test.sh. The handle is an opaque value; treat it as a token.

#include <cstdint>

namespace cheatah::window {

/**
 * Open a window with a Vulkan swapchain and the shaders/cool.slang plasma pipeline.
 * @param width window width in pixels.
 * @param height window height in pixels.
 * @return an opaque window handle, or 0 on failure.
 * @complexity O(1) amortised (one-time device + pipeline setup).
 * @alloc host structures for the window + Vulkan objects.
 * @gpualloc swapchain images + the pipeline live in GPU memory.
 * @destroy release it with close().
 */
long long open(long long width, long long height);

/**
 * Draw and present one frame of the plasma at animation time @p t.
 * @param handle the window returned by open().
 * @param t animation time in seconds.
 * @return 1 to keep going, 0 when the window was closed or an error occurred.
 * @complexity O(1) per frame.
 * @alloc none.
 */
long long draw(long long handle, double t);

/**
 * Destroy the window and every Vulkan object behind @p handle.
 * @param handle the window returned by open().
 * @complexity O(1).
 * @alloc none.
 */
void close(long long handle);

}  // namespace cheatah::window
