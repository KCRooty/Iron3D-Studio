#include "VulkanRenderer.h"
#include "core/Paths.h"

#define GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>

#include <cstdio>
#include <cstring>
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
    // El ShadowMap y el SsaoPass se crean ANTES que LitPipeline: esta ultima
    // necesita sus descriptor set layouts para construir el pipeline layout
    // (set=0 del sampler de sombras, set=1 del mapa de AO).
    shadowMap_.create(ctx_.device, ctx_.physicalDevice, allocator_, shaderDir);
    ssaoPass_.create(ctx_.device, ctx_.physicalDevice, allocator_, shaderDir, width, height);
    materialTextures_.create(ctx_.device, ctx_.physicalDevice, allocator_, ctx_.graphicsQueue,
                             ctx_.graphicsQueueFamily);
    litPipeline_.create(ctx_.device, ctx_.physicalDevice, swapchain_.renderPass(), shaderDir,
                         shadowMap_.descriptorSetLayout(), ssaoPass_.descriptorSetLayout(),
                         materialTextures_.descriptorSetLayout(), VK_CULL_MODE_NONE);
    gizmoLinePipeline_.create(ctx_.device, swapchain_.renderPass(), shaderDir,
                               VK_PRIMITIVE_TOPOLOGY_LINE_LIST);
    gizmoTriPipeline_.create(ctx_.device, swapchain_.renderPass(), shaderDir,
                              VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VK_CULL_MODE_NONE);
    gizmoMesh_.create(allocator_);
    gridMesh_.create(allocator_);

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
    gridMesh_.destroy(allocator_);
    gizmoLinePipeline_.destroy(ctx_.device);
    gizmoTriPipeline_.destroy(ctx_.device);
    litPipeline_.destroy(ctx_.device);
    shadowMap_.destroy(ctx_.device, allocator_);
    ssaoPass_.destroy(ctx_.device, allocator_);
    materialTextures_.destroy(ctx_.device, allocator_);
    for (auto& mesh : meshes_) mesh.destroy(ctx_.device, allocator_);
    meshes_.clear();
    pendingDestroy_.clear();
    freeMeshes_.clear();
    for (const auto& p : pendingUiDestroy_) freeUiTexture(p.tex);
    for (const auto& t : uiTextures_) freeUiTexture(t);
    pendingUiDestroy_.clear();
    uiTextures_.clear();
    if (uiSampler_) vkDestroySampler(ctx_.device, uiSampler_, nullptr);
    if (uploadPool_) vkDestroyCommandPool(ctx_.device, uploadPool_, nullptr);
    uiSampler_ = VK_NULL_HANDLE;
    uploadPool_ = VK_NULL_HANDLE;
    vmaDestroyAllocator(allocator_);

    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();

    swapchain_.destroy(ctx_.instance, ctx_.device); // tambien destruye la superficie (es wd_->Surface)
    ctx_.shutdown();
}

void VulkanRenderer::resizeIfNeeded(int fbWidth, int fbHeight) {
    swapchain_.resizeIfNeeded(ctx_.instance, ctx_.physicalDevice, ctx_.device,
                               ctx_.graphicsQueueFamily, fbWidth, fbHeight);
    ssaoPass_.resize(ctx_.device, ctx_.physicalDevice, allocator_, swapchain_.width(), swapchain_.height());
}

void VulkanRenderer::setClearColor(float r, float g, float b, float a) {
    swapchain_.setClearColor(r, g, b, a);
}

MeshHandle VulkanRenderer::createMesh(const MeshData& data) {
    PrimitiveMesh mesh;
    mesh.create(ctx_.device, allocator_, data);
    if (!freeMeshes_.empty()) {
        const MeshHandle h = freeMeshes_.back();
        freeMeshes_.pop_back();
        meshes_[h] = std::move(mesh);
        return h;
    }
    meshes_.push_back(std::move(mesh));
    return (MeshHandle)(meshes_.size() - 1);
}

void VulkanRenderer::destroyMesh(MeshHandle handle) {
    if (handle >= meshes_.size()) return;
    // 4 frames: mas que los frames en vuelo del swapchain, con margen.
    pendingDestroy_.push_back({handle, 4});
}

bool VulkanRenderer::beginFrame() {
    if (swapchain_.isMinimized()) return false;
    for (size_t i = 0; i < pendingDestroy_.size();) {
        if (--pendingDestroy_[i].framesLeft <= 0) {
            meshes_[pendingDestroy_[i].handle].destroy(ctx_.device, allocator_);
            freeMeshes_.push_back(pendingDestroy_[i].handle);
            pendingDestroy_[i] = pendingDestroy_.back();
            pendingDestroy_.pop_back();
        } else {
            ++i;
        }
    }
    for (size_t i = 0; i < pendingUiDestroy_.size();) {
        if (--pendingUiDestroy_[i].framesLeft <= 0) {
            freeUiTexture(pendingUiDestroy_[i].tex);
            pendingUiDestroy_[i] = pendingUiDestroy_.back();
            pendingUiDestroy_.pop_back();
        } else {
            ++i;
        }
    }
    // Solo adquiere imagen + abre el command buffer -- el render pass
    // principal lo abre beginMainPass(), despues de la pasada de sombras
    // (ver comentario de SwapchainWindow::beginFrame()).
    currentCmd_ = swapchain_.beginFrame(ctx_.device);
    return currentCmd_ != VK_NULL_HANDLE;
}

void VulkanRenderer::beginShadowPass(const glm::mat4& lightViewProj) {
    lightViewProj_ = lightViewProj;
    shadowMap_.beginPass(currentCmd_);
}

void VulkanRenderer::drawMeshShadow(MeshHandle mesh, const glm::mat4& model) {
    if (mesh >= meshes_.size()) return;
    shadowMap_.pushLightMvp(currentCmd_, lightViewProj_ * model);
    meshes_[mesh].draw(currentCmd_);
}

void VulkanRenderer::endShadowPass() {
    shadowMap_.endPass(currentCmd_);
}

void VulkanRenderer::beginGBufferPass(const glm::mat4& view, const glm::mat4& proj) {
    ssaoPass_.beginGBufferPass(currentCmd_, view, proj);
}

void VulkanRenderer::drawMeshGBuffer(MeshHandle mesh, const glm::mat4& model) {
    if (mesh >= meshes_.size()) return;
    ssaoPass_.drawMeshGBuffer(currentCmd_, model);
    meshes_[mesh].draw(currentCmd_);
}

void VulkanRenderer::endGBufferPass() {
    ssaoPass_.endGBufferPass(currentCmd_);
}

void VulkanRenderer::beginMainPass() {
    swapchain_.beginRenderPass(currentCmd_);
    // ImGui fija su propio viewport/scissor al componer, asi que basta con
    // fijar el del visor aqui para todo lo que se dibuja antes de endFrame().
    if (vpW_ > 0 && vpH_ > 0) {
        VkViewport viewport{(float)vpX_, (float)vpY_, (float)vpW_, (float)vpH_, 0.0f, 1.0f};
        VkRect2D scissor{{vpX_, vpY_}, {(uint32_t)vpW_, (uint32_t)vpH_}};
        vkCmdSetViewport(currentCmd_, 0, 1, &viewport);
        vkCmdSetScissor(currentCmd_, 0, 1, &scissor);
    }
}

void VulkanRenderer::setViewportRect(int x, int y, int width, int height) {
    vpX_ = x < 0 ? 0 : x;
    vpY_ = y < 0 ? 0 : y;
    vpW_ = width < 0 ? 0 : width;
    vpH_ = height < 0 ? 0 : height;
}

void VulkanRenderer::setDrawRect(int x, int y, int width, int height) {
    if (currentCmd_ == VK_NULL_HANDLE || width <= 0 || height <= 0) return;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    VkViewport viewport{(float)x, (float)y, (float)width, (float)height, 0.0f, 1.0f};
    VkRect2D scissor{{x, y}, {(uint32_t)width, (uint32_t)height}};
    vkCmdSetViewport(currentCmd_, 0, 1, &viewport);
    vkCmdSetScissor(currentCmd_, 0, 1, &scissor);
}

void VulkanRenderer::drawGrid(const std::vector<Vertex>& lineVerts, const glm::mat4& viewProj) {
    gridMesh_.upload(lineVerts, {});
    gizmoLinePipeline_.bind(currentCmd_);
    gizmoLinePipeline_.pushMvp(currentCmd_, viewProj);
    gridMesh_.drawLines(currentCmd_);
}

void VulkanRenderer::setLighting(const glm::vec3& lightPosWS, const glm::vec3& eyePosWS,
                                  const glm::mat4& lightViewProj) {
    lightPosWS_ = lightPosWS;
    eyePosWS_ = eyePosWS;
    lightViewProj_ = lightViewProj;
}

void VulkanRenderer::drawMesh(MeshHandle mesh, const glm::mat4& model, const glm::mat4& mvp,
                               const Material& material, const TexMapping& tex) {
    if (mesh >= meshes_.size()) return;
    // Textura de Color (2026-11-10): si el material la tiene y la etiqueta la
    // aplica en UVW; si no se puede cargar, color plano como hasta ahora.
    VkDescriptorSet texSet = materialTextures_.whiteSet();
    int texFlags = 0;
    glm::vec4 uvTransform(1.0f, 1.0f, 0.0f, 0.0f);
    if (tex.enabled) {
        if (VkDescriptorSet s = materialTextures_.get(material.texture(TexSlot::Color), material.texBaseDir)) {
            texSet = s;
            texFlags = kLitTexColor | (tex.tile ? kLitTexTile : 0);
            uvTransform = texMappingUvTransform(tex);
        }
    }
    // Rebind siempre (no asume que nada mas dejo la pipeline comun activa):
    // drawGizmo cambia de pipeline, asi que el orden de llamadas no puede
    // importar.
    litPipeline_.bind(currentCmd_);
    litPipeline_.bindShadowMap(currentCmd_, shadowMap_.descriptorSet());
    litPipeline_.bindAoMap(currentCmd_, ssaoPass_.descriptorSet());
    litPipeline_.bindColorTexture(currentCmd_, texSet);
    litPipeline_.pushConstants(currentCmd_, model, mvp, lightPosWS_, eyePosWS_, material, lightViewProj_, texFlags,
                               uvTransform);
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

// --- Texturas de la UI ---------------------------------------------------------
// Subida sincrona (copia desde un buffer de paso + vkQueueWaitIdle): solo se
// usa para miniaturas pequenas, unas pocas por frame (ContentBrowser.cpp).

void* VulkanRenderer::createUiTexture(const uint8_t* rgba, int width, int height) {
    if (!rgba || width <= 0 || height <= 0) return nullptr;
    VkDevice dev = ctx_.device;
    if (!uiSampler_) {
        VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        si.magFilter = VK_FILTER_LINEAR;
        si.minFilter = VK_FILTER_LINEAR;
        si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        si.addressModeU = si.addressModeV = si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        si.maxLod = 1.0f;
        vkCheck(vkCreateSampler(dev, &si, nullptr, &uiSampler_), "vkCreateSampler (UI)");
    }
    if (!uploadPool_) {
        VkCommandPoolCreateInfo pi{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        pi.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        pi.queueFamilyIndex = ctx_.graphicsQueueFamily;
        vkCheck(vkCreateCommandPool(dev, &pi, nullptr, &uploadPool_), "vkCreateCommandPool (UI)");
    }

    const VkDeviceSize bytes = VkDeviceSize(width) * VkDeviceSize(height) * 4;
    VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bi.size = bytes;
    bi.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    VmaAllocationCreateInfo bai{};
    bai.usage = VMA_MEMORY_USAGE_AUTO;
    bai.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
    VkBuffer staging = VK_NULL_HANDLE;
    VmaAllocation stagingAlloc = VK_NULL_HANDLE;
    VmaAllocationInfo stagingInfo{};
    if (vmaCreateBuffer(allocator_, &bi, &bai, &staging, &stagingAlloc, &stagingInfo) != VK_SUCCESS) return nullptr;
    std::memcpy(stagingInfo.pMappedData, rgba, size_t(bytes));
    vmaFlushAllocation(allocator_, stagingAlloc, 0, VK_WHOLE_SIZE);

    UiTexture t;
    VkImageCreateInfo ii{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ii.imageType = VK_IMAGE_TYPE_2D;
    ii.format = VK_FORMAT_R8G8B8A8_UNORM; // el swapchain es UNORM: sin conversion sRGB
    ii.extent = {uint32_t(width), uint32_t(height), 1};
    ii.mipLevels = 1;
    ii.arrayLayers = 1;
    ii.samples = VK_SAMPLE_COUNT_1_BIT;
    ii.tiling = VK_IMAGE_TILING_OPTIMAL;
    ii.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VmaAllocationCreateInfo iai{};
    iai.usage = VMA_MEMORY_USAGE_AUTO;
    if (vmaCreateImage(allocator_, &ii, &iai, &t.image, &t.alloc, nullptr) != VK_SUCCESS) {
        vmaDestroyBuffer(allocator_, staging, stagingAlloc);
        return nullptr;
    }

    VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    ai.commandPool = uploadPool_;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    vkCheck(vkAllocateCommandBuffers(dev, &ai, &cmd), "vkAllocateCommandBuffers (UI)");
    VkCommandBufferBeginInfo cbi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    cbi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &cbi);

    VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = t.image;
    b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    b.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    b.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                         nullptr, 1, &b);
    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent = ii.extent;
    vkCmdCopyBufferToImage(cmd, staging, t.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    b.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    b.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr,
                         0, nullptr, 1, &b);
    vkEndCommandBuffer(cmd);
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cmd;
    vkCheck(vkQueueSubmit(ctx_.graphicsQueue, 1, &submit, VK_NULL_HANDLE), "vkQueueSubmit (UI)");
    vkCheck(vkQueueWaitIdle(ctx_.graphicsQueue), "vkQueueWaitIdle (UI)");
    vkFreeCommandBuffers(dev, uploadPool_, 1, &cmd);
    vmaDestroyBuffer(allocator_, staging, stagingAlloc);

    VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vi.image = t.image;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = ii.format;
    vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCheck(vkCreateImageView(dev, &vi, nullptr, &t.view), "vkCreateImageView (UI)");
    t.set = ImGui_ImplVulkan_AddTexture(uiSampler_, t.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    if (!t.set) {
        freeUiTexture(t);
        return nullptr;
    }
    uiTextures_.push_back(t);
    return reinterpret_cast<void*>(t.set);
}

void VulkanRenderer::destroyUiTexture(void* texture) {
    const auto set = reinterpret_cast<VkDescriptorSet>(texture);
    for (size_t i = 0; i < uiTextures_.size(); ++i) {
        if (uiTextures_[i].set != set) continue;
        // Los frames en vuelo pueden seguir muestreandola: se libera despues.
        pendingUiDestroy_.push_back({uiTextures_[i], 4});
        uiTextures_[i] = uiTextures_.back();
        uiTextures_.pop_back();
        return;
    }
}

void VulkanRenderer::freeUiTexture(const UiTexture& t) {
    if (t.set) ImGui_ImplVulkan_RemoveTexture(t.set);
    if (t.view) vkDestroyImageView(ctx_.device, t.view, nullptr);
    if (t.image) vmaDestroyImage(allocator_, t.image, t.alloc);
}

} // namespace iron3d
