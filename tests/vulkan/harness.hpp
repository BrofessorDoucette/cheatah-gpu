#pragma once

// Handwritten test harness for the GENERATED per-function Vulkan tests + the handwritten behavioral
// tests. The VulkanPresence fixture brings up volk + an instance + a logical device on EVERY physical
// device ONCE for the whole suite (SetUpTestSuite), so each generated TEST_F is a fast probe and they
// show up as hundreds of individually-named tests. Device bring-up is handwritten on purpose: it is
// exactly the stateful setup codegen shouldn't guess at.

#include <volk.h>

#include <gtest/gtest.h>

#include <cstdint>
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

/// Fixture for the generated presence tests: a Vulkan instance + a logical device per physical device,
/// created once and shared by every TEST_F. `inline static` members keep it header-safe across TUs.
class VulkanPresence : public ::testing::Test {
  protected:
    inline static VkInstance inst_ = VK_NULL_HANDLE;
    inline static std::vector<Device> devices_{};

    static void SetUpTestSuite() {
        if (volkInitialize() != VK_SUCCESS) {
            return;
        }
        VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
        app.apiVersion = VK_API_VERSION_1_0;
        VkInstanceCreateInfo ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
        ci.pApplicationInfo = &app;
        if (vkCreateInstance(&ci, nullptr, &inst_) != VK_SUCCESS) {
            return;
        }
        volkLoadInstance(inst_);
        uint32_t n = 0;
        vkEnumeratePhysicalDevices(inst_, &n, nullptr);
        std::vector<VkPhysicalDevice> phys(n);
        vkEnumeratePhysicalDevices(inst_, &n, phys.data());
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
            if (vkCreateDevice(p, &dci, nullptr, &d.dev) == VK_SUCCESS) {
                devices_.push_back(d);
            }
        }
    }

    static void TearDownTestSuite() {
        for (auto& d : devices_) {
            vkDestroyDevice(d.dev, nullptr);
        }
        devices_.clear();
        if (inst_ != VK_NULL_HANDLE) {
            vkDestroyInstance(inst_, nullptr);
            inst_ = VK_NULL_HANDLE;
        }
    }

    /// Probe one entry point across every device. Capability-aware + NON-BLOCKING: a device whose
    /// version is below @p min_api, or that simply doesn't implement the command (null pointer — a
    /// reported apiVersion does not guarantee every command, e.g. llvmpipe advertises 1.4 yet omits
    /// some dynamic-state entry points), is skipped. The symbol's existence is already proven at link
    /// time; this confirms it loads where supported, on real hardware. Records how many devices have
    /// it, and only requires that at least one device was available to probe.
    void probe(const char* name, void** fpp, uint32_t min_api) {
        ASSERT_FALSE(devices_.empty()) << "no Vulkan devices available to probe " << name;
        int supported = 0;
        for (auto& d : devices_) {
            volkLoadDevice(d.dev);  // *fpp now reflects THIS device
            if (d.api >= min_api && *fpp != nullptr) {
                ++supported;
            }
        }
        volkLoadInstance(inst_);  // restore instance-level dispatch
        RecordProperty("devices_supporting", supported);
    }
};

}  // namespace cheatah::gpu::vktest
