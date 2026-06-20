#pragma once

/**
 * @file metal.hpp
 * @brief gpu.metal loader — pulls in Apple's metal-cpp (the real Metal C++ API).
 *
 * On Apple this resolves to the system Metal + Foundation frameworks. Off Apple, the platform shim
 * (gpu/metal/shim, on the include path) lets the SAME metal-cpp headers compile, and the software
 * emulator (gpu/metal/emulated) provides the runtime so a compute kernel actually runs on the CPU.
 * The cheatah-facing `mtl.<Name>` aliases that sit on top of this are in @ref types.hpp.
 *
 * metal-cpp needs Clang blocks and `<cmath>` available; the Metal build sets `-fblocks` and force-
 * includes `<cmath>` (Apple pulls these transitively). See cmake/Metal.cmake / scripts/metal_gate.sh.
 */

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>
