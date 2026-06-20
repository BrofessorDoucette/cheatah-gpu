# cheatah-gpu — the GPU standard-library extension for cheatah.
#
# One shared interface over the native GPU APIs (Vulkan + Metal), so a cheatah user can get on their
# GPU — for any purpose — with the simplest possible setup. The backend is selected at COMPILE TIME
# (see gpu/backend.hpp) so a delivered binary never carries bloat for an API it doesn't use.
#
# The `gpu` package is hand-authored, header-only C++20 (concept-constrained, statically typed) in
# the `cheatah::gpu::*` namespaces. A downstream project opts in with `biome add cheatah-gpu`, then
# `import gpu.dispatch` (etc.).
#
# ZERO runtime dependencies for the core: like cheatah itself, the seed pulls in nothing external.
# The Vulkan backend (roadmap) adds volk + VMA (CMake-fetched) and the system Vulkan loader/driver;
# those are wired behind CHEATAH_GPU_BUILD_VULKAN (default OFF) so the library + QA gate build with
# no GPU present. Windowing is intentionally NOT here (a consumer concern — GLFW/SDL); GLFW is a
# test-only dependency for the presentation tests.
cmake_minimum_required(VERSION 3.24)
project(cheatah-gpu LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# --- The `gpu` package, surfaced as a header-only INTERFACE target -----------------------------
# Consumers resolve `import gpu.dispatch` from these headers on purrc's import path; nothing to
# compile or link. The include dir is the repo root so `gpu/gpu.hpp` & `gpu/<mod>/...` resolve
# exactly as purrc expects.
add_library(cheatah_gpu INTERFACE)
add_library(cheatah::gpu ALIAS cheatah_gpu)
target_include_directories(cheatah_gpu INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>)
target_compile_features(cheatah_gpu INTERFACE cxx_std_20)

# Everything below is for building/testing THIS repo standalone; when cheatah-gpu is fetched as a
# sub-project (the biome path) the parent only wants the headers above.
if(NOT PROJECT_IS_TOP_LEVEL)
    return()
endif()

# A single, predictable output tree (so scripts/coverage.sh finds ./bin/cheatah_gpu_tests).
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# --- Standalone dev: wire git hooks so a push runs the qa_gate ---------------------------------
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.git" OR EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../.git")
    execute_process(
        COMMAND git config core.hooksPath .githooks
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        OUTPUT_QUIET ERROR_QUIET)
    message(STATUS "cheatah-gpu: git pre-push hook -> scripts/qa_gate.sh")
endif()

# Let the qa_gate find the sibling toolchain (purrc + cheatah runtime); override to relocate it.
set(CHEATAH_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../cheatah" CACHE PATH "cheatah toolchain checkout")

# --- Options -----------------------------------------------------------------------------------
option(CHEATAH_GPU_BUILD_TESTS "Build the C++ unit tests (fetches GoogleTest)" OFF)
option(CHEATAH_GPU_SANITIZE    "Build the unit tests with ASan + UBSan" OFF)
option(CHEATAH_GPU_BUILD_VULKAN "Build the Vulkan backend (fetches volk + VMA, provisions the GPU stack)" OFF)

# --- The Vulkan backend's dependencies (roadmap; OFF by default so the gate needs no GPU) -------
if(CHEATAH_GPU_BUILD_VULKAN)
    include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/Vulkan.cmake)
endif()

# --- C++ unit tests: GoogleTest, fetched ONLY under this option --------------------------------
include(FetchContent)
if(CHEATAH_GPU_BUILD_TESTS)
    FetchContent_Declare(googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG        52eb8108c5bdec04579160ae17225d66034bd723)  # pinned (matches ../cheatah)
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
    enable_testing()
    add_subdirectory(tests)
endif()

# --- The cheatah (.purr) system suite, run through the qa_gate as one ctest ---------------------
include(CTest)
if(BUILD_TESTING)
    add_test(NAME qa_gate
             COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/scripts/qa_gate.sh
             WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    set_tests_properties(qa_gate PROPERTIES ENVIRONMENT "CHEATAH_DIR=${CHEATAH_DIR}")
endif()
