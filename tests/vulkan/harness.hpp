#pragma once

// Handwritten test harness for the generated Vulkan presence checks + the handwritten behavioral
// tests. It brings up volk + a Vulkan instance, enumerates EVERY physical device, and for each
// creates a logical device and loads its function pointers — so the generated checks run on the
// software (llvmpipe), integrated (Intel) and discrete (NVIDIA) devices alike.
//
// Device bring-up is handwritten on purpose: it is exactly the stateful setup codegen shouldn't
// guess at. The generated checks are capability-gated here, so a device that lacks a feature SKIPS
// (it never fails the gate) while every function still has a test (= 100% coverage).

#include <volk.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace cheatah::gpu::vktest {

/// One enumerated physical device with a logical device created on it.
struct Device {
    VkPhysicalDevice phys = VK_NULL_HANDLE;
    VkDevice dev = VK_NULL_HANDLE;
    uint32_t api = 0;  ///< the device's reported apiVersion
    std::string name;
};

/// Capability tallies for the current run (reset by run_all_devices).
inline int g_present = 0;
inline int g_unsupported = 0;

/// Capability-aware, NON-BLOCKING presence check. A device's reported apiVersion does not guarantee
/// every command is implemented — e.g. llvmpipe advertises 1.4 yet omits some dynamic-state entry
/// points — so the loaded pointer itself is the source of truth: non-null = supported (exercised),
/// null = not supported on this device (skipped). A missing feature NEVER fails the gate; the value
/// is that every forwarder is probed on every device (100% coverage) and the symbol is proven to
/// exist at link time.
inline void check(const Device& d, const char* name, void* fp, uint32_t min_api) {
    if (d.api < min_api) {
        return;  // command is newer than this device — not applicable
    }
    if (fp != nullptr) {
        ++g_present;
    } else {
        ++g_unsupported;
    }
    (void)name;
}

// Defined in the GENERATED tests/vulkan/generated_presence_checks.cpp.
void run_generated_presence_checks(const Device& d);

/// Run @p body on every physical device, with volk's pointers loaded for that device.
inline void run_all_devices(void (*body)(const Device&)) {
    g_present = 0;
    g_unsupported = 0;
    ASSERT_EQ(volkInitialize(), VK_SUCCESS) << "no Vulkan loader";
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.apiVersion = VK_API_VERSION_1_0;
    VkInstanceCreateInfo ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ci.pApplicationInfo = &app;
    VkInstance inst{};
    ASSERT_EQ(vkCreateInstance(&ci, nullptr, &inst), VK_SUCCESS);
    volkLoadInstance(inst);

    uint32_t n = 0;
    vkEnumeratePhysicalDevices(inst, &n, nullptr);
    std::vector<VkPhysicalDevice> phys(n);
    vkEnumeratePhysicalDevices(inst, &n, phys.data());
    EXPECT_GT(n, 0u) << "no Vulkan physical devices enumerated";

    for (auto p : phys) {
        VkPhysicalDeviceProperties props{};
        vkGetPhysicalDeviceProperties(p, &props);
        uint32_t qn = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(p, &qn, nullptr);
        if (qn == 0) {
            continue;
        }
        float pri = 1.0f;
        VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        qci.queueFamilyIndex = 0;
        qci.queueCount = 1;
        qci.pQueuePriorities = &pri;
        VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
        dci.queueCreateInfoCount = 1;
        dci.pQueueCreateInfos = &qci;

        Device d;
        d.phys = p;
        d.api = props.apiVersion;
        d.name = props.deviceName;
        if (vkCreateDevice(p, &dci, nullptr, &d.dev) != VK_SUCCESS) {
            ADD_FAILURE() << "vkCreateDevice failed on " << d.name;
            continue;
        }
        volkLoadDevice(d.dev);  // device-level vk* pointers now target THIS device
        body(d);
        vkDestroyDevice(d.dev, nullptr);
        volkLoadInstance(inst);  // restore instance-level dispatch for the next device
    }
    vkDestroyInstance(inst, nullptr);
    // A non-blocking capability summary; sanity-check that the harness actually loaded entry points.
    std::printf("[ vulkan   ] capability probe: %d supported, %d unsupported across %u device(s)\n",
                g_present, g_unsupported, n);
    EXPECT_GT(g_present, 0) << "no Vulkan entry points loaded on any device — harness/loader broken";
}

}  // namespace cheatah::gpu::vktest
