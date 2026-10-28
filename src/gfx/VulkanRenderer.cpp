#include "VulkanRenderer.h"
#include "core/Paths.h"

#define GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>

#include <cstdio>
#include <cstdlib>

namespace iron3d {

namespace {
void checkVkResultCb(VkResult err) {
    if (err == 0) return;
    std::fprintf(stderr, "[Iron3D][vulkan] VkResult = %d\n", (int)err);
    if (err < 0) std::abort();
}
} // namespace

void VulkanRenderer::init(GLFWwindow* window, int width, int height) {
    if (!glfwVulkanSupported()) {
        std::fprintf(stderr, "[Iron3D] Esta maquina no expone Vulkan via GLFW.\n");
        std::abort();
    }

    uint32_t glfwExtCount = 0;
    const char** glfwExt = glfwGetRequiredInstanceExtensions(&glfwExtCount);
    std::vector<const char*> instanceExtensions(glfwExt, glfwExt + glfwExtCount);
    ctx_.init(instanceExtensions);

    VkSurfaceKHR surface = VK_NULL_HANDLE;
    vkCheck(glfwCreateWindowSurface(ctx_.instance, window, nullptr, &surface), "glfwCreateWindowSurface");

    swapchain_.init(ctx_.instance, ctx_.physicalDevice, ctx_.device, ctx_.graphicsQueueFamily,
                     surface, width, height, /*minImageCount=*/2);

    VmaAllocatorCreateInfo allocatorInfo{};
    allocatorInfo.physicalDevice = ctx_.physicalDevice;
    allocatorInfo.device = ctx_.device;
    allocatorInfo.instance = ctx_.instance;
    allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_3;
    vkCheck(vmaCreateAllocator(&allocatorInfo, &allocator_), "vmaCreateAllocator");

    // cullMode=NONE en la malla comun: ver el comentario historico de esta
    // decision en el commit de la Fase 2c (no vale la pena verificar a mano
    // el orden de vertices de cada primitiva nueva).
    const std::filesystem::path shaderDir = exeDirectory() / "shaders";
    litPipeline_.create(ctx_.device, ctx_.physicalDevice, swapchain_.renderPass(), shaderDir,
                         VK_CULL_MODE_NONE);
    gizmoLinePipeline_.create(ctx_.device, swapchain_.renderPass(), shaderDir,
                               VK_PRIMITIVE_TOPOLOGY_LINE_LIST);
    gizmoTriPipeline_.create(ctx_.device, swapchain_.renderPass(), shaderDir,
                              VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VK_CULL_MODE_NONE);
    gizmoMesh_.create(allocator_);

    ImGui_ImplGlfw_InitForVulkan(window, true);

    ImGui_ImplVulkan_InitInfo initInfo{};
    initInfo.Instance = ctx_.instance;
    initInfo.PhysicalDevice = ctx_.physicalDevice;
    initInfo.Device = ctx_.device;
    initInfo.QueueFamily = ctx_.graphicsQueueFamily;
    initInfo.Queue = ctx_.graphicsQueue;
    initInfo.DescriptorPool = ctx_.imguiDescriptorPool;
    initInfo.RenderPass = swapchain_.renderPass();
    initInfo.Subpass = 0;
    initInfo.MinImageCount = 2;
    initInfo.ImageCount = swapchain_.imageCount();
    initInfo.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    initInfo.Allocator = nullptr;
    initInfo.CheckVkResultFn = checkVkResultCb;
    initInfo.MinAllocationSize = 1024 * 1024;
    ImGui_ImplVulkan_Init(&initInfo);
}

void VulkanRenderer::shutdown() {
    vkCheck(vkDeviceWaitIdle(ctx_.device), "vkDeviceWaitIdle");

    gizmoMesh_.destroy(allocator_);
    gizmoLinePipeline_.destroy(ctx_.device);
    gizmoTriPipeline_.destroy(ctx_.device);
    litPipeline_.destroy(ctx_.device);
    for (auto& mesh : meshes_) mesh.destroy(ctx_.device, allocator_);
    meshes_.clear();
    vmaDestroyAllocator(allocator_);

    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();

    swapchain_.destroy(ctx_.instance, ctx_.device); // tambien destruye la superficie (es wd_->Surface)
    ctx_.shutdown();
}

void VulkanRenderer::resizeIfNeeded(int fbWidth, int fbHeight) {
    swapchain_.resizeIfNeeded(ctx_.instance, ctx_.physicalDevice, ctx_.device,
                               ctx_.graphicsQueueFamily, fbWidth, fbHeight);
}

void VulkanRenderer::setClearColor(float r, float g, float b, float a) {
    swapchain_.setClearColor(r, g, b, a);
}

MeshHandle VulkanRenderer::createMesh(const MeshData& data) {
    PrimitiveMesh mesh;
    mesh.create(ctx_.device, allocator_, data);
    meshes_.push_back(std::move(mesh));
    return (MeshHandle)(meshes_.size() - 1);
}

void VulkanRenderer::destroyMesh(MeshHandle handle) {
    if (handle >= meshes_.size()) return;
    meshes_[handle].destroy(ctx_.device, allocator_);
}

bool VulkanRenderer::beginFrame() {
    if (swapchain_.isMinimized()) return false;
    currentCmd_ = swapchain_.beginFrame(ctx_.device);
    return currentCmd_ != VK_NULL_HANDLE;
}

void VulkanRenderer::setLighting(const glm::vec3& lightPosWS, const glm::vec3& eyePosWS) {
    lightPosWS_ = lightPosWS;
    eyePosWS_ = eyePosWS;
}

void VulkanRenderer::drawMesh(MeshHandle mesh, const glm::mat4& model, const glm::mat4& mvp,
                               const Material& material) {
    if (mesh >= meshes_.size()) return;
    // Rebind siempre (no asume que nada mas dejo la pipeline comun activa):
    // drawGizmo cambia de pipeline, asi que el orden de llamadas no puede
    // importar.
    litPipeline_.bind(currentCmd_);
    litPipeline_.pushConstants(currentCmd_, model, mvp, lightPosWS_, eyePosWS_, material);
    meshes_[mesh].draw(currentCmd_);
}

void VulkanRenderer::drawGizmo(const std::vector<Vertex>& lineVerts, const std::vector<Vertex>& triVerts,
                                const glm::mat4& viewProj) {
    gizmoMesh_.upload(lineVerts, triVerts);

    gizmoTriPipeline_.bind(currentCmd_);
    gizmoTriPipeline_.pushMvp(currentCmd_, viewProj); // vertices del gizmo ya en espacio de mundo
    gizmoMesh_.drawTriangles(currentCmd_);

    gizmoLinePipeline_.bind(currentCmd_);
    gizmoLinePipeline_.pushMvp(currentCmd_, viewProj);
    gizmoMesh_.drawLines(currentCmd_);
}

void VulkanRenderer::endFrame() {
    swapchain_.endFrame(ctx_.device, ctx_.graphicsQueue, ImGui::GetDrawData());
    currentCmd_ = VK_NULL_HANDLE;
}

void VulkanRenderer::imguiNewFrame() {
    ImGui_ImplVulkan_NewFrame();
}

} // namespace iron3d
