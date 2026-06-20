# cmake/Vulkan.cmake — provision the GPU stack for the Vulkan backend (roadmap; included only when
# CHEATAH_GPU_BUILD_VULKAN=ON). The goal: a fresh machine builds and runs without manual setup.
#
# This is the BUILD's job (per the project's design): the build knows the target, so it selects the
# backend and fetches/finds whatever is missing. Nothing here is required for the header-only seed or
# the QA gate — they build with no GPU present.
include_guard(GLOBAL)
include(FetchContent)

# volk (meta-loader) and VMA (allocator) are header/source deps we always vendor via CPM/FetchContent
# so they're never a manual step. Pin tags deliberately when this backend lands.
FetchContent_Declare(volk
    GIT_REPOSITORY https://github.com/zeux/volk.git
    GIT_TAG        1.3.270)
FetchContent_Declare(VulkanMemoryAllocator
    GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git
    GIT_TAG        v3.1.0)

# The Vulkan loader + headers are system-level. Find them; if absent, fetch just the headers (volk
# loads the loader at runtime) and route the user to scripts/install-deps.sh for the loader/driver.
find_package(Vulkan QUIET)
if(NOT Vulkan_FOUND)
    message(STATUS "cheatah-gpu: Vulkan loader not found — fetching Vulkan-Headers; "
                   "run scripts/install-deps.sh to install the loader + a GPU driver.")
    FetchContent_Declare(VulkanHeaders
        GIT_REPOSITORY https://github.com/KhronosGroup/Vulkan-Headers.git
        GIT_TAG        v1.3.290)
    FetchContent_MakeAvailable(VulkanHeaders)
endif()

# --- Apple: prefer native Metal; fall back to MoltenVK only if Metal isn't available ------------
# cheatah-gpu prefers the native Metal backend on Apple. MoltenVK (Vulkan-over-Metal) is pulled in
# ONLY when the native Metal backend is unavailable/unimplemented, so we never ship the translation
# layer when the better native path exists.
if(APPLE)
    if(NOT DEFINED CHEATAH_GPU_METAL_OK)
        # Until the native Metal backend (gpu.metal) lands and verifies, Metal is "not OK" yet, so
        # Apple builds use the MoltenVK Vulkan path.
        set(CHEATAH_GPU_METAL_OK OFF)
    endif()
    if(NOT CHEATAH_GPU_METAL_OK)
        message(STATUS "cheatah-gpu: native Metal backend unavailable — using MoltenVK (Vulkan on Metal).")
        # MoltenVK ships in the Vulkan SDK (found via find_package(Vulkan) above) or can be fetched.
        # The real backend will link it here once gpu.vulkan is implemented.
    else()
        message(STATUS "cheatah-gpu: using the native Metal backend (no MoltenVK).")
    endif()
endif()

FetchContent_MakeAvailable(volk VulkanMemoryAllocator)
if(Vulkan_FOUND)
    message(STATUS "cheatah-gpu: Vulkan backend deps ready (volk + VMA + system Vulkan loader).")
else()
    message(STATUS "cheatah-gpu: Vulkan backend deps ready (volk + VMA + fetched Vulkan-Headers).")
endif()
