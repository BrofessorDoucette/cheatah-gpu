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
    GIT_TAG        1.4.350)  # >= the vendored vk.xml/SDK (1.4.341) so all 1.4 entry points are declared
FetchContent_Declare(VulkanMemoryAllocator
    GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git
    GIT_TAG        v3.1.0)

# --- Always prefer the NEWEST installed Vulkan SDK (self-heals on uninstall/reinstall) ----------
# Search the standard SDK roots, natural-sort the version dirs, and take the highest — no hardcoded
# path. This makes find_package(Vulkan) below (and, through compile_commands.json, the editor) track
# whatever the newest SDK is. An explicit $VULKAN_SDK or system install still works; this only adds
# the newest discovered SDK as the top search hint, so reinstalling a newer SDK is picked up on the
# next configure.
set(_vk_roots "$ENV{HOME}/Tools/vulkan-sdk" "$ENV{HOME}/VulkanSDK"
              "/opt/vulkan-sdk" "/opt/VulkanSDK" "C:/VulkanSDK")
set(_vk_found "")
foreach(_root IN LISTS _vk_roots)
    if(IS_DIRECTORY "${_root}")
        file(GLOB _vers RELATIVE "${_root}" "${_root}/*")
        foreach(_v IN LISTS _vers)
            if(NOT _v MATCHES "^[0-9]")            # only version-named dirs (skip "Releases", etc.)
                continue()
            endif()
            # The headers live in <ver>/x86_64/include (Linux SDK) or <ver>/include. Require the real
            # vulkan_core.h so we never pick an installer/stub dir.
            set(_inc "")
            if(EXISTS "${_root}/${_v}/x86_64/include/vulkan/vulkan_core.h")
                set(_inc "${_root}/${_v}/x86_64/include")
            elseif(EXISTS "${_root}/${_v}/include/vulkan/vulkan_core.h")
                set(_inc "${_root}/${_v}/include")
            endif()
            if(_inc)
                list(APPEND _vk_found "${_v}=${_inc}")
            endif()
        endforeach()
    endif()
endforeach()
if(_vk_found)
    list(SORT _vk_found COMPARE NATURAL ORDER DESCENDING)   # highest version first
    list(GET _vk_found 0 _vk_entry)
    string(REGEX REPLACE "^[^=]+=" "" _vk_inc "${_vk_entry}")        # the include dir
    get_filename_component(_vk_sdk "${_vk_inc}" DIRECTORY)           # the SDK arch root
    set(ENV{VULKAN_SDK} "${_vk_sdk}")
    list(PREPEND CMAKE_PREFIX_PATH "${_vk_sdk}")
    # Exported so the Vulkan test target includes the NEWEST headers ahead of any older system headers
    # — otherwise the latest commands (the 1.4 promotions) compile out and go untested.
    set(CHEATAH_GPU_VULKAN_INCLUDE "${_vk_inc}" CACHE INTERNAL "newest Vulkan SDK include")
    message(STATUS "cheatah-gpu: newest Vulkan SDK include -> ${_vk_inc}")
endif()

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
