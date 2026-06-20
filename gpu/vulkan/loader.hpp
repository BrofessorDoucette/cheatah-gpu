#pragma once

/**
 * @file loader.hpp
 * @brief Backend include for the generated Vulkan forwarders — volk when available.
 *
 * The cheatah-gpu design loads Vulkan through the **volk** meta-loader (no link-time loader
 * dependency, faster device dispatch). When volk's header is on the include path we use it (and the
 * `vk*` symbols are volk's function pointers); otherwise we fall back to the system Vulkan headers so
 * the generated surface still compiles on a plain SDK install. Either way the forwarders in
 * `commands.hpp` resolve to the real `vk*` entry point at runtime.
 *
 * A real app calls `volkInitialize()` once, then `volkLoadInstance(instance)` / `volkLoadDevice(
 * device)` so the pointers point at the selected driver before using the forwarders.
 */

#if __has_include(<volk.h>)
#  include <volk.h>
#else
#  include <vulkan/vulkan.h>
#endif
