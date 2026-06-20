// metal_impl.cpp — the single translation unit that emits metal-cpp's out-of-line definitions, for
// REAL Apple builds (links the system Metal + Foundation frameworks). Off Apple, the software emulator
// (gpu/metal/emulated/emulated_metal.cpp) defines these instead, so this file is compiled ONLY on
// Apple by scripts/metal_gate.sh.
#define NS_PRIVATE_IMPLEMENTATION
#define MTL_PRIVATE_IMPLEMENTATION
#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>
