// window.cpp — the implementation behind the `window` cheatah module (tests/window/window.hpp):
// bring up a GLFW window + a minimal Vulkan 1.3 dynamic-rendering pipeline that draws the
// shaders/cool.slang plasma, behind open/draw/close in `cheatah::window`. cheatah calls these
// directly — no escape hatches. The handle the module hands cheatah is just the Window pointer as an
// integer token; all the Vulkan/pointer detail stays here.
//
// Loads the SPIR-V from COOL_SPV_PATH (slangc compiles shaders/cool.slang at build time). The same
// Slang source also targets Metal for Apple.

#include "window.hpp"

#define GLFW_INCLUDE_NONE
#include <volk.h>

#include <GLFW/glfw3.h>

#include <cstdint>
#include <cstdio>
#include <vector>

#ifndef COOL_SPV_PATH
#define COOL_SPV_PATH "cool.spv"
#endif

namespace {

struct Window {
    GLFWwindow* glfw = nullptr;
    VkInstance instance = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkPhysicalDevice phys = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    uint32_t qfamily = 0;
    VkQueue queue = VK_NULL_HANDLE;
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkFormat format = VK_FORMAT_B8G8R8A8_UNORM;
    VkExtent2D extent{};
    std::vector<VkImage> images;
    std::vector<VkImageView> views;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkCommandPool pool = VK_NULL_HANDLE;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VkSemaphore acquired = VK_NULL_HANDLE;
    VkSemaphore rendered = VK_NULL_HANDLE;
    VkFence inflight = VK_NULL_HANDLE;
};

#define VKOK(x)                                                                                    \
    do {                                                                                           \
        if ((x) != VK_SUCCESS) {                                                                   \
            std::fprintf(stderr, "window: %s failed\n", #x);                                       \
            return nullptr;                                                                        \
        }                                                                                          \
    } while (0)

std::vector<uint32_t> read_spv(const char* path) {
    std::vector<uint32_t> out;
    FILE* f = std::fopen(path, "rb");
    if (!f) {
        std::fprintf(stderr, "window: cannot open SPIR-V '%s'\n", path);
        return out;
    }
    std::fseek(f, 0, SEEK_END);
    long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    out.resize(static_cast<size_t>(n) / 4);
    if (std::fread(out.data(), 1, static_cast<size_t>(n), f) != static_cast<size_t>(n)) out.clear();
    std::fclose(f);
    return out;
}

void barrier(VkCommandBuffer cmd, VkImage img, VkImageLayout from, VkImageLayout to,
             VkPipelineStageFlags src_stage, VkPipelineStageFlags dst_stage, VkAccessFlags src_acc,
             VkAccessFlags dst_acc) {
    VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    b.oldLayout = from; b.newLayout = to; b.image = img;
    b.srcAccessMask = src_acc; b.dstAccessMask = dst_acc;
    b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdPipelineBarrier(cmd, src_stage, dst_stage, 0, 0, nullptr, 0, nullptr, 1, &b);
}

Window* open_impl(int width, int height) {
    if (volkInitialize() != VK_SUCCESS) {
        std::fprintf(stderr, "window: volkInitialize failed (no Vulkan loader)\n");
        return nullptr;
    }
    if (!glfwInit() || !glfwVulkanSupported()) {
        std::fprintf(stderr, "window: GLFW has no Vulkan support\n");
        return nullptr;
    }
    auto* w = new Window();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    w->glfw = glfwCreateWindow(width, height, "cheatah-gpu \xc2\xb7 slang plasma", nullptr, nullptr);
    if (!w->glfw) { std::fprintf(stderr, "window: glfwCreateWindow failed\n"); return nullptr; }

    uint32_t n_ext = 0;
    const char** gext = glfwGetRequiredInstanceExtensions(&n_ext);
    std::vector<const char*> exts(gext, gext + n_ext);
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.apiVersion = VK_API_VERSION_1_3;
    VkInstanceCreateInfo ici{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ici.pApplicationInfo = &app;
    ici.enabledExtensionCount = static_cast<uint32_t>(exts.size());
    ici.ppEnabledExtensionNames = exts.data();
    VKOK(vkCreateInstance(&ici, nullptr, &w->instance));
    volkLoadInstance(w->instance);
    VKOK(glfwCreateWindowSurface(w->instance, w->glfw, nullptr, &w->surface));

    uint32_t nd = 0;
    vkEnumeratePhysicalDevices(w->instance, &nd, nullptr);
    std::vector<VkPhysicalDevice> devs(nd);
    vkEnumeratePhysicalDevices(w->instance, &nd, devs.data());
    bool picked = false;
    for (auto d : devs) {
        uint32_t nq = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(d, &nq, nullptr);
        std::vector<VkQueueFamilyProperties> qf(nq);
        vkGetPhysicalDeviceQueueFamilyProperties(d, &nq, qf.data());
        for (uint32_t i = 0; i < nq; ++i) {
            VkBool32 present = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(d, i, w->surface, &present);
            if ((qf[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && present) {
                w->phys = d; w->qfamily = i; picked = true; break;
            }
        }
        if (picked) break;
    }
    if (!picked) { std::fprintf(stderr, "window: no graphics+present queue\n"); return nullptr; }

    float pri = 1.0f;
    VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    qci.queueFamilyIndex = w->qfamily; qci.queueCount = 1; qci.pQueuePriorities = &pri;
    const char* dext[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    VkPhysicalDeviceVulkan13Features f13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    f13.dynamicRendering = VK_TRUE;
    VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    dci.pNext = &f13; dci.queueCreateInfoCount = 1; dci.pQueueCreateInfos = &qci;
    dci.enabledExtensionCount = 1; dci.ppEnabledExtensionNames = dext;
    VKOK(vkCreateDevice(w->phys, &dci, nullptr, &w->device));
    volkLoadDevice(w->device);
    vkGetDeviceQueue(w->device, w->qfamily, 0, &w->queue);

    VkSurfaceCapabilitiesKHR caps{};
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(w->phys, w->surface, &caps);
    w->extent = caps.currentExtent.width != 0xFFFFFFFFu
                    ? caps.currentExtent
                    : VkExtent2D{static_cast<uint32_t>(width), static_cast<uint32_t>(height)};
    uint32_t img_count = caps.minImageCount + 1;
    if (caps.maxImageCount && img_count > caps.maxImageCount) img_count = caps.maxImageCount;
    VkSwapchainCreateInfoKHR sci{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    sci.surface = w->surface; sci.minImageCount = img_count; sci.imageFormat = w->format;
    sci.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR; sci.imageExtent = w->extent;
    sci.imageArrayLayers = 1; sci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    sci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE; sci.preTransform = caps.currentTransform;
    sci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR; sci.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    sci.clipped = VK_TRUE;
    VKOK(vkCreateSwapchainKHR(w->device, &sci, nullptr, &w->swapchain));
    uint32_t sc_n = 0;
    vkGetSwapchainImagesKHR(w->device, w->swapchain, &sc_n, nullptr);
    w->images.resize(sc_n);
    vkGetSwapchainImagesKHR(w->device, w->swapchain, &sc_n, w->images.data());
    w->views.resize(sc_n);
    for (uint32_t i = 0; i < sc_n; ++i) {
        VkImageViewCreateInfo vci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        vci.image = w->images[i]; vci.viewType = VK_IMAGE_VIEW_TYPE_2D; vci.format = w->format;
        vci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        VKOK(vkCreateImageView(w->device, &vci, nullptr, &w->views[i]));
    }

    auto spv = read_spv(COOL_SPV_PATH);
    if (spv.empty()) return nullptr;
    VkShaderModuleCreateInfo smci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    smci.codeSize = spv.size() * 4; smci.pCode = spv.data();
    VkShaderModule mod = VK_NULL_HANDLE;
    VKOK(vkCreateShaderModule(w->device, &smci, nullptr, &mod));
    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT; stages[0].module = mod; stages[0].pName = "vertexMain";
    stages[1] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT; stages[1].module = mod; stages[1].pName = "fragmentMain";
    VkPushConstantRange pcr{VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(float)};
    VkPipelineLayoutCreateInfo plci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    plci.pushConstantRangeCount = 1; plci.pPushConstantRanges = &pcr;
    VKOK(vkCreatePipelineLayout(w->device, &plci, nullptr, &w->layout));
    VkPipelineVertexInputStateCreateInfo vin{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vp.viewportCount = 1; vp.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rs.polygonMode = VK_POLYGON_MODE_FILL; rs.cullMode = VK_CULL_MODE_NONE; rs.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineColorBlendAttachmentState cba{};
    cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                         VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    cb.attachmentCount = 1; cb.pAttachments = &cba;
    VkDynamicState dyn[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    ds.dynamicStateCount = 2; ds.pDynamicStates = dyn;
    VkPipelineRenderingCreateInfo prci{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    prci.colorAttachmentCount = 1; prci.pColorAttachmentFormats = &w->format;
    VkGraphicsPipelineCreateInfo gpci{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    gpci.pNext = &prci; gpci.stageCount = 2; gpci.pStages = stages;
    gpci.pVertexInputState = &vin; gpci.pInputAssemblyState = &ia; gpci.pViewportState = &vp;
    gpci.pRasterizationState = &rs; gpci.pMultisampleState = &ms; gpci.pColorBlendState = &cb;
    gpci.pDynamicState = &ds; gpci.layout = w->layout;
    VKOK(vkCreateGraphicsPipelines(w->device, VK_NULL_HANDLE, 1, &gpci, nullptr, &w->pipeline));
    vkDestroyShaderModule(w->device, mod, nullptr);

    VkCommandPoolCreateInfo cpci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    cpci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT; cpci.queueFamilyIndex = w->qfamily;
    VKOK(vkCreateCommandPool(w->device, &cpci, nullptr, &w->pool));
    VkCommandBufferAllocateInfo cbai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cbai.commandPool = w->pool; cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; cbai.commandBufferCount = 1;
    VKOK(vkAllocateCommandBuffers(w->device, &cbai, &w->cmd));
    VkSemaphoreCreateInfo semci{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VKOK(vkCreateSemaphore(w->device, &semci, nullptr, &w->acquired));
    VKOK(vkCreateSemaphore(w->device, &semci, nullptr, &w->rendered));
    VkFenceCreateInfo fci{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    VKOK(vkCreateFence(w->device, &fci, nullptr, &w->inflight));
    return w;
}

int draw_impl(Window* w, float t) {
    if (glfwWindowShouldClose(w->glfw)) return 0;
    glfwPollEvents();
    vkWaitForFences(w->device, 1, &w->inflight, VK_TRUE, UINT64_MAX);
    uint32_t idx = 0;
    VkResult acq = vkAcquireNextImageKHR(w->device, w->swapchain, UINT64_MAX, w->acquired,
                                         VK_NULL_HANDLE, &idx);
    if (acq == VK_ERROR_OUT_OF_DATE_KHR) return 1;
    if (acq != VK_SUCCESS && acq != VK_SUBOPTIMAL_KHR) return 0;
    vkResetFences(w->device, 1, &w->inflight);
    vkResetCommandBuffer(w->cmd, 0);
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    vkBeginCommandBuffer(w->cmd, &bi);
    barrier(w->cmd, w->images[idx], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0,
            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
    VkRenderingAttachmentInfo color{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    color.imageView = w->views[idx]; color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color.clearValue.color = {{0.0f, 0.0f, 0.0f, 1.0f}};
    VkRenderingInfo ri{VK_STRUCTURE_TYPE_RENDERING_INFO};
    ri.renderArea = {{0, 0}, w->extent}; ri.layerCount = 1;
    ri.colorAttachmentCount = 1; ri.pColorAttachments = &color;
    vkCmdBeginRendering(w->cmd, &ri);
    VkViewport vpt{0, 0, static_cast<float>(w->extent.width), static_cast<float>(w->extent.height), 0, 1};
    VkRect2D sc{{0, 0}, w->extent};
    vkCmdSetViewport(w->cmd, 0, 1, &vpt);
    vkCmdSetScissor(w->cmd, 0, 1, &sc);
    vkCmdBindPipeline(w->cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, w->pipeline);
    vkCmdPushConstants(w->cmd, w->layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(float), &t);
    vkCmdDraw(w->cmd, 3, 1, 0, 0);
    vkCmdEndRendering(w->cmd);
    barrier(w->cmd, w->images[idx], VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, 0);
    vkEndCommandBuffer(w->cmd);
    VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.waitSemaphoreCount = 1; si.pWaitSemaphores = &w->acquired; si.pWaitDstStageMask = &wait_stage;
    si.commandBufferCount = 1; si.pCommandBuffers = &w->cmd;
    si.signalSemaphoreCount = 1; si.pSignalSemaphores = &w->rendered;
    vkQueueSubmit(w->queue, 1, &si, w->inflight);
    VkPresentInfoKHR pi{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    pi.waitSemaphoreCount = 1; pi.pWaitSemaphores = &w->rendered;
    pi.swapchainCount = 1; pi.pSwapchains = &w->swapchain; pi.pImageIndices = &idx;
    vkQueuePresentKHR(w->queue, &pi);
    return 1;
}

void close_impl(Window* w) {
    if (w->device) vkDeviceWaitIdle(w->device);
    if (w->inflight) vkDestroyFence(w->device, w->inflight, nullptr);
    if (w->acquired) vkDestroySemaphore(w->device, w->acquired, nullptr);
    if (w->rendered) vkDestroySemaphore(w->device, w->rendered, nullptr);
    if (w->pool) vkDestroyCommandPool(w->device, w->pool, nullptr);
    if (w->pipeline) vkDestroyPipeline(w->device, w->pipeline, nullptr);
    if (w->layout) vkDestroyPipelineLayout(w->device, w->layout, nullptr);
    for (auto v : w->views) vkDestroyImageView(w->device, v, nullptr);
    if (w->swapchain) vkDestroySwapchainKHR(w->device, w->swapchain, nullptr);
    if (w->device) vkDestroyDevice(w->device, nullptr);
    if (w->surface) vkDestroySurfaceKHR(w->instance, w->surface, nullptr);
    if (w->instance) vkDestroyInstance(w->instance, nullptr);
    if (w->glfw) glfwDestroyWindow(w->glfw);
    glfwTerminate();
    delete w;
}

}  // namespace

namespace cheatah::window {

long long open(long long width, long long height) {
    Window* w = open_impl(static_cast<int>(width), static_cast<int>(height));
    return static_cast<long long>(reinterpret_cast<uintptr_t>(w));
}

long long draw(long long handle, double t) {
    Window* w = reinterpret_cast<Window*>(static_cast<uintptr_t>(handle));
    return w ? draw_impl(w, static_cast<float>(t)) : 0;
}

void close(long long handle) {
    Window* w = reinterpret_cast<Window*>(static_cast<uintptr_t>(handle));
    if (w) close_impl(w);
}

}  // namespace cheatah::window
