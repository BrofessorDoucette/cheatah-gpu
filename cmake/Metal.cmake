# cmake/Metal.cmake — provision the native Metal backend (metal-cpp bindings + build settings).
# Included only when CHEATAH_GPU_BUILD_METAL=ON. The CMake analogue of scripts/metal_gate.sh: it
# locates Apple's metal-cpp header library, records the include dir and the compile settings the
# bindings need, and leaves tests/CMakeLists.txt to apply them to the Metal test targets.
#
# NO third-party downloads. metal-cpp is Apple's OWN MIT-licensed C++ interface to Metal (from
# https://developer.apple.com/metal/cpp/). We never fetch it from a mirror; you provide it — either
# vendored in-repo, or via a path to a copy you downloaded from Apple. If it is absent we stop with
# instructions rather than reaching out to the network.
#
# metal-cpp is header-only C++ over the Metal/Foundation Objective-C API. It needs Clang blocks
# (-fblocks) and <cmath> visible; exactly ONE translation unit emits its out-of-line definitions
# (NS/MTL_PRIVATE_IMPLEMENTATION). On Apple that TU is tests/metal/metal_impl.cpp and the build links
# the Metal/Foundation/QuartzCore frameworks; off Apple the software emulator (gpu/metal/emulated)
# provides the runtime and a small Objective-C/CoreFoundation shim (gpu/metal/shim) lets the SAME
# headers compile — so the Metal path builds + runs (bit-identical) on every platform for verification.

# Search order for the metal-cpp headers (first hit wins). None of these touch the network:
#   1. -DCHEATAH_GPU_METAL_CPP=/path  (or the env var of the same name) — a copy you downloaded
#      from Apple, or your Xcode's metal-cpp.
#   2. third_party/metal-cpp/ vendored inside this repo (commit Apple's headers there for zero config).
if(NOT CHEATAH_GPU_METAL_CPP AND DEFINED ENV{CHEATAH_GPU_METAL_CPP})
    set(CHEATAH_GPU_METAL_CPP "$ENV{CHEATAH_GPU_METAL_CPP}")
endif()
set(_metal_cpp_candidates
    "${CHEATAH_GPU_METAL_CPP}"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/metal-cpp")
set(_metal_cpp_inc "")
foreach(_cand IN LISTS _metal_cpp_candidates)
    if(_cand AND EXISTS "${_cand}/Metal/Metal.hpp")
        set(_metal_cpp_inc "${_cand}")
        break()
    endif()
endforeach()

if(NOT _metal_cpp_inc)
    message(FATAL_ERROR
        "cheatah-gpu: Metal backend requested (CHEATAH_GPU_BUILD_METAL=ON) but metal-cpp was not found.\n"
        "metal-cpp is Apple's official C++ interface to Metal (MIT-licensed). We do NOT download it —\n"
        "get it from Apple and point us at it:\n"
        "  • download 'metal-cpp' from https://developer.apple.com/metal/cpp/ , unzip it, then either\n"
        "      configure with  -DCHEATAH_GPU_METAL_CPP=/path/to/metal-cpp\n"
        "      or set the env var  CHEATAH_GPU_METAL_CPP=/path/to/metal-cpp\n"
        "  • or vendor it in-repo at  third_party/metal-cpp/  (so it needs no configuration).\n"
        "The folder must contain Metal/Metal.hpp and Foundation/Foundation.hpp.")
endif()

# Exposed to tests/CMakeLists.txt (the Metal test target consumes both).
set(CHEATAH_GPU_METAL_CPP_INCLUDE "${_metal_cpp_inc}" CACHE INTERNAL "metal-cpp include dir")
# metal-cpp needs Clang blocks and <cmath> visible in every Metal TU (Apple pulls these
# transitively; elsewhere we ask for them explicitly). Same flags scripts/metal_gate.sh uses.
set(CHEATAH_GPU_METAL_CXXFLAGS -fblocks -include cmath CACHE INTERNAL "metal-cpp compile flags")

message(STATUS "cheatah-gpu: metal-cpp -> ${_metal_cpp_inc}")
if(APPLE)
    message(STATUS "cheatah-gpu: Metal backend ready (native Metal/Foundation/QuartzCore frameworks).")
else()
    message(STATUS "cheatah-gpu: Metal backend ready (software emulator + shim; no Apple HW).")
endif()
