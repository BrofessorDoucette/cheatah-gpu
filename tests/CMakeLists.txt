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
# One GoogleTest binary — the ecosystem standard, the same shape as the Vulkan suite; the gtest exit
# code is the truth (no output-grepping). scripts/metal_gate.sh drives this SAME binary (plus its
# Valgrind/Helgrind tiers). On Apple it links the Metal frameworks and runs on the GPU; off Apple it
# compiles the software emulator + Objective-C/CoreFoundation shim and runs the same flow on the CPU,
# bit-identically. The emulator's leak counter is compiled in (CHEATAH_GPU_METAL_LEAKCHECK) so every
# fixture's TearDown can assert leak-cleanliness — a check that would otherwise be a constant 0.
# metal-cpp headers + flags come from cmake/Metal.cmake (CHEATAH_GPU_METAL_CPP_INCLUDE / _CXXFLAGS).
if(CHEATAH_GPU_BUILD_METAL)
    if(APPLE)
        set(_metal_backend metal/metal_impl.cpp)          # the TU that emits metal-cpp's defs
    else()
        set(_metal_backend
            ${CMAKE_SOURCE_DIR}/gpu/metal/emulated/emulated_metal.cpp
            ${CMAKE_SOURCE_DIR}/gpu/metal/emulated/block_stubs.cpp)
    endif()
    add_executable(cheatah_gpu_metal_tests
        metal/metal_compute_test.cpp
        metal/metal_multiline_test.cpp
        metal/metal_texture_test.cpp
        ${_metal_backend})
    target_include_directories(cheatah_gpu_metal_tests PRIVATE
        ${CMAKE_SOURCE_DIR}                               # for "gpu/metal/types.hpp"
        ${CMAKE_CURRENT_SOURCE_DIR}/metal                 # for "harness.hpp"
        ${CHEATAH_GPU_METAL_CPP_INCLUDE})                 # metal-cpp headers
    target_compile_options(cheatah_gpu_metal_tests PRIVATE ${CHEATAH_GPU_METAL_CXXFLAGS})
    target_compile_definitions(cheatah_gpu_metal_tests PRIVATE CHEATAH_GPU_METAL_LEAKCHECK=1)
    target_link_libraries(cheatah_gpu_metal_tests PRIVATE GTest::gtest_main)
    if(APPLE)
        target_link_libraries(cheatah_gpu_metal_tests PRIVATE
            "-framework Metal" "-framework Foundation" "-framework QuartzCore")
    else()
        target_include_directories(cheatah_gpu_metal_tests PRIVATE ${CMAKE_SOURCE_DIR}/gpu/metal/shim)
    endif()
    if(CHEATAH_GPU_SANITIZE)
        # -fno-sanitize=function: metal-cpp calls objc_msgSend through a typed function-pointer
        # cast (its documented mechanism, not a bug), which the UBSan `function` check would flag.
        target_compile_options(cheatah_gpu_metal_tests PRIVATE
            -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize=function)
        target_link_options(cheatah_gpu_metal_tests PRIVATE -fsanitize=address,undefined)
    endif()
    # The historical mtl:* ctest names, each scoped to one gtest suite of the binary. (The vk: tests
    # use gtest_discover_tests; here the three stable names are part of the gate's interface, so they
    # are registered by hand as filter-scoped invocations.)
    # A filter that matches nothing exits 0, so each entry also fails on gtest's own zero-selection
    # banner — the one output check kept, guarding a suite rename from silently emptying a ctest.
    foreach(_suite Compute Multiline Texture)
        string(TOLOWER ${_suite} _name)
        add_test(NAME mtl:${_name}
                 COMMAND cheatah_gpu_metal_tests --gtest_filter=Metal${_suite}.*)
        set_tests_properties(mtl:${_name} PROPERTIES
            FAIL_REGULAR_EXPRESSION "0 tests from 0 test suites")
    endforeach()
endif()
