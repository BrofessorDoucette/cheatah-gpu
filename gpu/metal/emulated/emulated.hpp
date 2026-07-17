#pragma once

/**
 * @file emulated.hpp
 * @brief Control surface for the software-emulated Metal device (off-Apple testing).
 *
 * Off Apple there is no Metal framework and no Metal shader compiler, so cheatah-gpu ships a software
 * Metal runtime (gpu/metal/emulated/emulated_metal.cpp) that lets metal-cpp compile AND run a compute
 * kernel on the CPU — the Metal analogue of Mesa llvmpipe for Vulkan. Because real MSL cannot be
 * compiled here, a host registers a C++ stand-in kernel by name; a Metal dispatch of a pipeline built
 * for that function name runs it over the bound buffers.
 *
 * This is a normal cheatah module (`cheatah::gpu::metal::emulated`), NOT a C-linkage shim — only the
 * Objective-C runtime entry points metal-cpp links against keep C linkage, internally.
 */

namespace cheatah::gpu::metal::emulated {

/**
 * A three-axis extent, mirroring `MTL::Size` — width × height × depth, unused axes 1.
 * @test MetalEmulated.Grid3d
 */
struct Extent {
    unsigned long width = 1;   ///< threads along x.
    unsigned long height = 1;  ///< threads along y; 1 when unused.
    unsigned long depth = 1;   ///< threads along z; 1 when unused.
};

/**
 * The full shape of one dispatch, as a 3-D-aware stand-in kernel receives it. `threads` is the
 * TOTAL thread grid per axis for BOTH dispatch forms — `dispatchThreads` passes its grid through;
 * `dispatchThreadgroups` is normalized to groups × threadsPerThreadgroup per axis (what real Metal
 * launches) — so a kernel loops `threads` without caring which form dispatched it. There is no
 * threadgroup-memory emulation: a C++ stand-in shares the whole address space and needs none
 * (MSL `threadgroup` tiles are an Apple-hardware concern).
 * @test MetalEmulated.Grid3d
 * @test MetalEmulated.Threadgroups
 */
struct DispatchShape {
    Extent threads;                  ///< total threads per axis (normalized, both dispatch forms).
    Extent threads_per_threadgroup;  ///< the second `MTL::Size` the dispatch call passed.
};

/**
 * Register a software stand-in for a compiled MSL compute kernel, keyed by its function name.
 * @param name the kernel/function name a pipeline is built for (matches newFunctionWithName).
 * @param fn   called per dispatch as fn(buffers, buffer_count, grid_width): `buffers[i]` is the bound
 *             buffer at index `i` (or null), `grid_width` is the dispatched thread count along x.
 * @complexity O(1) registration.
 * @alloc none (stores the function pointer).
 * @test MetalEmulated.AddArrays
 * @systest systests/metal/test_compute.purr
 */
void register_kernel(const char* name, void (*fn)(void** buffers, unsigned buffer_count, unsigned long grid_width));

/**
 * Register a 3-D-aware stand-in kernel — the @ref DispatchShape overload. Same name-keyed table as
 * the 1-D form (a later registration under the same name replaces an earlier one, either form); use
 * this for kernels that span y/z or need the threadgroup split.
 * @param name the kernel/function name a pipeline is built for (matches newFunctionWithName).
 * @param fn   called per dispatch as fn(buffers, buffer_count, shape): `buffers[i]` is the bound
 *             buffer at index `i` (or null), `shape` the dispatch's full @ref DispatchShape.
 * @complexity O(1) registration.
 * @alloc none (stores the function pointer).
 * @test MetalEmulated.Grid3d
 * @test MetalEmulated.Threadgroups
 */
void register_kernel(const char* name,
                     void (*fn)(void** buffers, unsigned buffer_count, const DispatchShape& shape));

/**
 * The number of emulator objects still alive — 0 means nothing leaked. Meaningful when built with
 * `-DCHEATAH_GPU_METAL_LEAKCHECK=1` (otherwise always 0); compiled out for production so there is no
 * tracking overhead. This ALERTS on leaks; it does not enforce RAII.
 * @return count of live emulated Metal objects.
 * @complexity O(1).
 * @alloc none.
 * @test MetalEmulated.LeakClean
 * @systest systests/metal/test_compute.purr
 */
unsigned long live_objects();

}  // namespace cheatah::gpu::metal::emulated
