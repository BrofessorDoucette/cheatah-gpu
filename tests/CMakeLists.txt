# C++ unit tests for the gpu package's hand-authored headers.
#
# The single binary `cheatah_gpu_tests` is what scripts/coverage.sh instruments (clang source-based
# coverage) and what security/run-valgrind.sh memchecks. Linking the cheatah::gpu INTERFACE target
# puts the repo root on the include path so `#include "gpu/dispatch/dispatch.hpp"` resolves.
include(GoogleTest)

add_executable(cheatah_gpu_tests
    dispatch_test.cpp
    backend_test.cpp
)
target_link_libraries(cheatah_gpu_tests PRIVATE
    cheatah::gpu
    GTest::gtest_main
)

# Optional ASan + UBSan (the `asan` preset turns this on). Valgrind runs the plain debug build.
if(CHEATAH_GPU_SANITIZE)
    target_compile_options(cheatah_gpu_tests PRIVATE
        -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(cheatah_gpu_tests PRIVATE
        -fsanitize=address,undefined)
endif()

gtest_discover_tests(cheatah_gpu_tests)

# --- Vulkan tests (need volk + a real device; built only with CHEATAH_GPU_BUILD_VULKAN) ----------
# Generated presence checks (100% of the forwarder surface, capability-gated) + handwritten
# behavioral tests, run across every enumerated device by the harness.
if(CHEATAH_GPU_BUILD_VULKAN)
    add_executable(cheatah_gpu_vulkan_tests
        vulkan/generated_presence_checks.cpp
        vulkan/handwritten_test.cpp
    )
    target_compile_definitions(cheatah_gpu_vulkan_tests PRIVATE VK_NO_PROTOTYPES)
    target_include_directories(cheatah_gpu_vulkan_tests PRIVATE
        ${CMAKE_SOURCE_DIR}                     # for "gpu/vulkan/commands.hpp"
        ${CMAKE_CURRENT_SOURCE_DIR}/vulkan      # for "harness.hpp"
    )
    # Use the NEWEST SDK headers ahead of any older system headers, so the latest commands (the 1.4
    # promotions) are compiled and tested — not silently #ifdef'd out.
    if(DEFINED CHEATAH_GPU_VULKAN_INCLUDE)
        target_include_directories(cheatah_gpu_vulkan_tests BEFORE PRIVATE ${CHEATAH_GPU_VULKAN_INCLUDE})
    endif()
    target_link_libraries(cheatah_gpu_vulkan_tests PRIVATE volk GTest::gtest_main)
    gtest_discover_tests(cheatah_gpu_vulkan_tests TEST_PREFIX "vk:")
endif()

# --- Metal tests (native Metal on Apple; software emulator elsewhere) -----------------------------
# The SAME test sources scripts/metal_gate.sh drives, now built by CMake under CHEATAH_GPU_BUILD_METAL.
# Each is a standalone program (own main, prints "RESULT: PASS"/"RESULT: FAIL", exits 0/1) — NOT a
# gtest binary. On Apple they link the Metal frameworks and run on the GPU; off Apple they compile the
# software emulator + Objective-C/CoreFoundation shim and run the same flow on the CPU, bit-identically.
# metal-cpp headers + flags come from cmake/Metal.cmake (CHEATAH_GPU_METAL_CPP_INCLUDE / _CXXFLAGS).
if(CHEATAH_GPU_BUILD_METAL)
    if(APPLE)
        set(_metal_backend metal/metal_impl.cpp)          # the TU that emits metal-cpp's defs
    else()
        set(_metal_backend
            ${CMAKE_SOURCE_DIR}/gpu/metal/emulated/emulated_metal.cpp
            ${CMAKE_SOURCE_DIR}/gpu/metal/emulated/block_stubs.cpp)
    endif()
    foreach(_mt compute multiline texture)
        set(_tgt cheatah_gpu_metal_${_mt}_test)
        add_executable(${_tgt} metal/metal_${_mt}_test.cpp ${_metal_backend})
        target_include_directories(${_tgt} PRIVATE
            ${CMAKE_SOURCE_DIR}                           # for "gpu/metal/types.hpp"
            ${CHEATAH_GPU_METAL_CPP_INCLUDE})             # metal-cpp headers
        target_compile_options(${_tgt} PRIVATE ${CHEATAH_GPU_METAL_CXXFLAGS})
        if(APPLE)
            target_link_libraries(${_tgt} PRIVATE
                "-framework Metal" "-framework Foundation" "-framework QuartzCore")
        else()
            target_include_directories(${_tgt} PRIVATE ${CMAKE_SOURCE_DIR}/gpu/metal/shim)
        endif()
        if(CHEATAH_GPU_SANITIZE)
            # -fno-sanitize=function: metal-cpp calls objc_msgSend through a typed function-pointer
            # cast (its documented mechanism, not a bug), which the UBSan `function` check would flag.
            target_compile_options(${_tgt} PRIVATE
                -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize=function)
            target_link_options(${_tgt} PRIVATE -fsanitize=address,undefined)
        endif()
        add_test(NAME mtl:${_mt} COMMAND ${_tgt})
        set_tests_properties(mtl:${_mt} PROPERTIES FAIL_REGULAR_EXPRESSION "RESULT: FAIL")
    endforeach()
endif()
