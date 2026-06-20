// Handwritten Vulkan tests: the runner for the generated presence checks, plus behavioral tests that
// drive the actual cheatah::gpu::vulkan forwarders (not raw vk*) — the cases codegen can't be trusted
// to exercise. Both are capability-aware: nothing here fails because hardware lacks a feature.

#include "harness.hpp"

#include "gpu/vulkan/commands.hpp"  // the generated forwarders under test

namespace vk = cheatah::gpu::vulkan;

namespace cheatah::gpu::vktest {

// (The generated per-function presence tests live in generated_presence_checks.cpp as one TEST_F
// each — 234 individually-named tests. The tests below are the handwritten behavioral ones.)

// Handwritten: drive instance + device lifecycle through OUR forwarders, honouring the @destroy
// contracts. Proves the forwarders actually call through to the real entry points and round-trip.
TEST(VulkanHandwritten, InstanceLifecycleViaForwarders) {
    ASSERT_EQ(volkInitialize(), VK_SUCCESS) << "no Vulkan loader";

    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.apiVersion = VK_API_VERSION_1_0;
    VkInstanceCreateInfo ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ci.pApplicationInfo = &app;

    VkInstance inst{};
    ASSERT_EQ(vk::CreateInstance(&ci, nullptr, &inst), VK_SUCCESS);  // forwarder
    volkLoadInstance(inst);

    uint32_t n = 0;
    EXPECT_EQ(vk::EnumeratePhysicalDevices(inst, &n, nullptr), VK_SUCCESS);  // forwarder
    EXPECT_GT(n, 0u) << "expected at least one Vulkan device";

    vk::DestroyInstance(inst, nullptr);  // forwarder — fulfils the @destroy contract
}

}  // namespace cheatah::gpu::vktest
