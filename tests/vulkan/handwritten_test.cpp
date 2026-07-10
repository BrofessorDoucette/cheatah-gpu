// Handwritten Vulkan tests: the runner for the generated presence checks, plus behavioral tests that
// drive the actual cheatah::gpu::vulkan forwarders (not raw vk*) — the cases codegen can't be trusted
// to exercise. Both are capability-aware: nothing here fails because hardware lacks a feature.

#include "harness.hpp"

#include "gpu/vulkan/commands.hpp"  // the generated forwarders under test
#include "gpu/vulkan/handles.hpp"   // the handwritten handle <-> token conversion

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

// Handwritten: the handle <-> token conversion (gpu/vulkan/handles.hpp). The generated overloads
// carry a token INTO a command; these carry a native handle back OUT — the direction out-params,
// handle arrays, and native struct fields need. A round trip must be the identity, and a token must
// be exactly what the generated cheatah-friendly overload already accepts.
TEST(VulkanHandles, RoundTrip) {
    ASSERT_EQ(volkInitialize(), VK_SUCCESS) << "no Vulkan loader";

    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.apiVersion = VK_API_VERSION_1_0;
    VkInstanceCreateInfo ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ci.pApplicationInfo = &app;

    VkInstance inst{};
    ASSERT_EQ(vk::CreateInstance(&ci, nullptr, &inst), VK_SUCCESS);  // out-param: a NATIVE handle
    volkLoadInstance(inst);

    const long long tok = vk::token(inst);  // the direction the overloads cannot express
    EXPECT_NE(tok, 0);
    EXPECT_EQ(vk::handle<VkInstance>(tok), inst);  // identity round trip

    uint32_t n = 0;
    EXPECT_EQ(vk::EnumeratePhysicalDevices(tok, &n, nullptr), VK_SUCCESS);  // token -> long long overload

    vk::DestroyInstance(tok, nullptr);
}

// A zeroed struct must read as "nothing created": the null handle is token 0, both ways, for a
// dispatchable handle (a pointer) and a non-dispatchable one (a pointer on 64-bit, uint64_t else).
TEST(VulkanHandles, NullHandleIsZeroToken) {
    EXPECT_EQ(vk::token(VkInstance{VK_NULL_HANDLE}), 0);
    EXPECT_EQ(vk::handle<VkInstance>(0), VkInstance{VK_NULL_HANDLE});
    EXPECT_EQ(vk::token(VkImage{VK_NULL_HANDLE}), 0);
    EXPECT_EQ(vk::handle<VkImage>(0), VkImage{VK_NULL_HANDLE});
}

}  // namespace cheatah::gpu::vktest
