#include "SsaoPass.h"
#include "VulkanContext.h"
#include "core/Vertex.h"

#include <fstream>
#include <vector>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstddef>

namespace iron3d {

namespace {

std::vector<char> readFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        std::fprintf(stderr, "[Iron3D] No se pudo abrir el shader: %s\n", path.string().c_str());
        std::abort();
    }
    const size_t size = (size_t)file.tellg();
    std::vector<char> buffer(size);
    file.seekg(0);
    file.read(buffer.data(), (std::streamsize)size);
    return buffer;
}

VkShaderModule createShaderModule(VkDevice device, const std::vector<char>& code) {
    VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    info.codeSize = code.size();
    info.pCode = reinterpret_cast<const uint32_t*>(code.data());
    VkShaderModule module = VK_NULL_HANDLE;
    vkCheck(vkCreateShaderModule(device, &info, nullptr, &module), "vkCreateShaderModule (ssao)");
    return module;
}

// Solo attachment (nunca se muestrea): mismo criterio de busqueda que
// SwapchainWindow::pickDepthFormat, duplicado aqui a proposito -- cada
// archivo de gfx/ es autocontenido (igual que ShadowPipeline/LitPipeline con
// sus propios readFile/createShaderModule locales).
VkFormat pickGBufferDepthFormat(VkPhysicalDevice physicalDevice) {
    const VkFormat candidates[] = {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT,
                                    VK_FORMAT_D24_UNORM_S8_UINT};
    for (VkFormat format : candidates) {
        VkFormatProperties props{};
        vkGetPhysicalDeviceFormatProperties(physicalDevice, format, &props);
        if (props.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) return format;
    }
    std::fprintf(stderr, "[Iron3D] Sin formato de profundidad valido para el G-buffer de SSAO.\n");
    std::abort();
}

void createColorImage(VkDevice device, VmaAllocator allocator, VkFormat format, int width, int height,
                       VkImage& image, VmaAllocation& allocation, VkImageView& view) {
    VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = format;
    imageInfo.extent = {(uint32_t)width, (uint32_t)height, 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    vkCheck(vmaCreateImage(allocator, &imageInfo, &allocInfo, &image, &allocation, nullptr),
            "vmaCreateImage (SsaoPass color)");

    VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    viewInfo.image = image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.layerCount = 1;
    vkCheck(vkCreateImageView(device, &viewInfo, nullptr, &view), "vkCreateImageView (SsaoPass color)");
}

// Dos dependencias EXTERNAL<->subpass0, mismo patron que ShadowMap::create:
// la primera espera a que la lectura-fragment-shader del uso ANTERIOR de
// este attachment (frame previo, u otra pasada ya cerrada en este mismo
// command buffer) termine antes de escribir; la segunda deja al siguiente
// consumidor (fragment shader) esperar a que esta escritura termine.
// extraStage/extraAccess cubren el attachment de profundidad extra que solo
// necesita el G-buffer (mismo acceso de escritura en ambas direcciones,
// porque la profundidad se escribe y descarta dentro de esta misma pasada).
void fillExternalDependencies(VkSubpassDependency deps[2], VkPipelineStageFlags extraStage = 0,
                               VkAccessFlags extraAccess = 0) {
    deps[0] = {};
    deps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    deps[0].dstSubpass = 0;
    deps[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    deps[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    deps[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | extraStage;
    deps[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | extraAccess;

    deps[1] = {};
    deps[1].srcSubpass = 0;
    deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    deps[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | extraStage;
    deps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | extraAccess;
    deps[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    deps[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
}

VkPipelineShaderStageCreateInfo shaderStage(VkShaderStageFlagBits stage, VkShaderModule module) {
    VkPipelineShaderStageCreateInfo info{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    info.stage = stage;
    info.module = module;
    info.pName = "main";
    return info;
}

} // namespace

void SsaoPass::create(VkDevice device, VkPhysicalDevice physicalDevice, VmaAllocator allocator,
                       const std::filesystem::path& shaderDir, int width, int height) {
    width_ = width;
    height_ = height;
    gbufferDepthFormat_ = pickGBufferDepthFormat(physicalDevice);

    VkSamplerCreateInfo samplerInfo{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.maxLod = 1.0f;
    vkCheck(vkCreateSampler(device, &samplerInfo, nullptr, &linearSampler_), "vkCreateSampler (SsaoPass)");

    createImages(device, allocator);
    createGBufferPass(device);
    createSsaoPass(device);
    createBlurPass(device);
    createFramebuffers(device);
    createGBufferPipeline(device, shaderDir);
    createSsaoPipeline(device, shaderDir);
    createBlurPipeline(device, shaderDir);
    createDescriptors(device);
    writeDescriptors(device);
}

void SsaoPass::destroy(VkDevice device, VmaAllocator allocator) {
    if (descriptorPool_) vkDestroyDescriptorPool(device, descriptorPool_, nullptr);
    if (ssaoInputSetLayout_) vkDestroyDescriptorSetLayout(device, ssaoInputSetLayout_, nullptr);
    if (blurInputSetLayout_) vkDestroyDescriptorSetLayout(device, blurInputSetLayout_, nullptr);
    if (aoOutputSetLayout_) vkDestroyDescriptorSetLayout(device, aoOutputSetLayout_, nullptr);

    if (gbufferPipeline_) vkDestroyPipeline(device, gbufferPipeline_, nullptr);
    if (gbufferLayout_) vkDestroyPipelineLayout(device, gbufferLayout_, nullptr);
    if (ssaoPipeline_) vkDestroyPipeline(device, ssaoPipeline_, nullptr);
    if (ssaoLayout_) vkDestroyPipelineLayout(device, ssaoLayout_, nullptr);
    if (blurPipeline_) vkDestroyPipeline(device, blurPipeline_, nullptr);
    if (blurLayout_) vkDestroyPipelineLayout(device, blurLayout_, nullptr);

    destroyFramebuffers(device);
    if (gbufferRenderPass_) vkDestroyRenderPass(device, gbufferRenderPass_, nullptr);
    if (ssaoRenderPass_) vkDestroyRenderPass(device, ssaoRenderPass_, nullptr);
    if (blurRenderPass_) vkDestroyRenderPass(device, blurRenderPass_, nullptr);

    destroyImages(device, allocator);
    if (linearSampler_) vkDestroySampler(device, linearSampler_, nullptr);

    gbufferPipeline_ = ssaoPipeline_ = blurPipeline_ = VK_NULL_HANDLE;
    gbufferLayout_ = ssaoLayout_ = blurLayout_ = VK_NULL_HANDLE;
    gbufferRenderPass_ = ssaoRenderPass_ = blurRenderPass_ = VK_NULL_HANDLE;
    ssaoInputSetLayout_ = blurInputSetLayout_ = aoOutputSetLayout_ = VK_NULL_HANDLE;
    descriptorPool_ = VK_NULL_HANDLE;
    linearSampler_ = VK_NULL_HANDLE;
}

void SsaoPass::resize(VkDevice device, VkPhysicalDevice physicalDevice, VmaAllocator allocator, int width,
                       int height) {
    (void)physicalDevice; // el formato de profundidad no cambia con el tamano, ya elegido en create()
    if (width == width_ && height == height_) return;
    if (width <= 0 || height <= 0) return;

    destroyFramebuffers(device);
    destroyImages(device, allocator);
    width_ = width;
    height_ = height;
    createImages(device, allocator);
    createFramebuffers(device);
    // Las vistas de imagen son handles nuevos tras recrearse -- los 3
    // descriptor sets (que las referencian por valor, no por puntero a esta
    // clase) quedan apuntando a vistas ya destruidas si no se reescriben.
    writeDescriptors(device);
}

void SsaoPass::createImages(VkDevice device, VmaAllocator allocator) {
    createColorImage(device, allocator, gbufferColorFormat_, width_, height_, gPosImage_, gPosAlloc_, gPosView_);
    createColorImage(device, allocator, gbufferColorFormat_, width_, height_, gNormalImage_, gNormalAlloc_,
                      gNormalView_);
    createColorImage(device, allocator, aoFormat_, width_, height_, aoRawImage_, aoRawAlloc_, aoRawView_);
    createColorImage(device, allocator, aoFormat_, width_, height_, aoBlurredImage_, aoBlurredAlloc_,
                      aoBlurredView_);

    VkImageCreateInfo depthInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    depthInfo.imageType = VK_IMAGE_TYPE_2D;
    depthInfo.format = gbufferDepthFormat_;
    depthInfo.extent = {(uint32_t)width_, (uint32_t)height_, 1};
    depthInfo.mipLevels = 1;
    depthInfo.arrayLayers = 1;
    depthInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    depthInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    depthInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    depthInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VmaAllocationCreateInfo depthAllocInfo{};
    depthAllocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    vkCheck(vmaCreateImage(allocator, &depthInfo, &depthAllocInfo, &gDepthImage_, &gDepthAlloc_, nullptr),
            "vmaCreateImage (SsaoPass depth)");

    const bool hasStencil =
        gbufferDepthFormat_ == VK_FORMAT_D32_SFLOAT_S8_UINT || gbufferDepthFormat_ == VK_FORMAT_D24_UNORM_S8_UINT;
    VkImageViewCreateInfo depthViewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    depthViewInfo.image = gDepthImage_;
    depthViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    depthViewInfo.format = gbufferDepthFormat_;
    depthViewInfo.subresourceRange.aspectMask =
        VK_IMAGE_ASPECT_DEPTH_BIT | (hasStencil ? VK_IMAGE_ASPECT_STENCIL_BIT : 0);
    depthViewInfo.subresourceRange.levelCount = 1;
    depthViewInfo.subresourceRange.layerCount = 1;
    vkCheck(vkCreateImageView(device, &depthViewInfo, nullptr, &gDepthView_), "vkCreateImageView (SsaoPass depth)");
}

void SsaoPass::destroyImages(VkDevice device, VmaAllocator allocator) {
    if (gPosView_) { vkDestroyImageView(device, gPosView_, nullptr); gPosView_ = VK_NULL_HANDLE; }
    if (gNormalView_) { vkDestroyImageView(device, gNormalView_, nullptr); gNormalView_ = VK_NULL_HANDLE; }
    if (gDepthView_) { vkDestroyImageView(device, gDepthView_, nullptr); gDepthView_ = VK_NULL_HANDLE; }
    if (aoRawView_) { vkDestroyImageView(device, aoRawView_, nullptr); aoRawView_ = VK_NULL_HANDLE; }
    if (aoBlurredView_) { vkDestroyImageView(device, aoBlurredView_, nullptr); aoBlurredView_ = VK_NULL_HANDLE; }

    if (gPosImage_) { vmaDestroyImage(allocator, gPosImage_, gPosAlloc_); gPosImage_ = VK_NULL_HANDLE; }
    if (gNormalImage_) { vmaDestroyImage(allocator, gNormalImage_, gNormalAlloc_); gNormalImage_ = VK_NULL_HANDLE; }
    if (gDepthImage_) { vmaDestroyImage(allocator, gDepthImage_, gDepthAlloc_); gDepthImage_ = VK_NULL_HANDLE; }
    if (aoRawImage_) { vmaDestroyImage(allocator, aoRawImage_, aoRawAlloc_); aoRawImage_ = VK_NULL_HANDLE; }
    if (aoBlurredImage_) { vmaDestroyImage(allocator, aoBlurredImage_, aoBlurredAlloc_); aoBlurredImage_ = VK_NULL_HANDLE; }
}

void SsaoPass::createGBufferPass(VkDevice device) {
    VkAttachmentDescription attachments[3]{};
    attachments[0] = {}; // gPos
    attachments[0].format = gbufferColorFormat_;
    attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[0].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    attachments[1] = attachments[0]; // gNormal, mismos parametros

    attachments[2] = {}; // depth, descartable -- nunca se muestrea despues
    attachments[2].format = gbufferDepthFormat_;
    attachments[2].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[2].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[2].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[2].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[2].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[2].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[2].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorRefs[2] = {{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
                                           {1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL}};
    VkAttachmentReference depthRef{2, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 2;
    subpass.pColorAttachments = colorRefs;
    subpass.pDepthStencilAttachment = &depthRef;

    VkSubpassDependency deps[2];
    fillExternalDependencies(deps, VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                              VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);

    VkRenderPassCreateInfo passInfo{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    passInfo.attachmentCount = 3;
    passInfo.pAttachments = attachments;
    passInfo.subpassCount = 1;
    passInfo.pSubpasses = &subpass;
    passInfo.dependencyCount = 2;
    passInfo.pDependencies = deps;
    vkCheck(vkCreateRenderPass(device, &passInfo, nullptr, &gbufferRenderPass_), "vkCreateRenderPass (gbuffer)");
}

void SsaoPass::createSsaoPass(VkDevice device) {
    VkAttachmentDescription attachment{};
    attachment.format = aoFormat_;
    attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkAttachmentReference colorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;

    VkSubpassDependency deps[2];
    fillExternalDependencies(deps);

    VkRenderPassCreateInfo passInfo{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    passInfo.attachmentCount = 1;
    passInfo.pAttachments = &attachment;
    passInfo.subpassCount = 1;
    passInfo.pSubpasses = &subpass;
    passInfo.dependencyCount = 2;
    passInfo.pDependencies = deps;
    vkCheck(vkCreateRenderPass(device, &passInfo, nullptr, &ssaoRenderPass_), "vkCreateRenderPass (ssao)");
}

void SsaoPass::createBlurPass(VkDevice device) {
    VkAttachmentDescription attachment{};
    attachment.format = aoFormat_;
    attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkAttachmentReference colorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;

    VkSubpassDependency deps[2];
    fillExternalDependencies(deps);

    VkRenderPassCreateInfo passInfo{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    passInfo.attachmentCount = 1;
    passInfo.pAttachments = &attachment;
    passInfo.subpassCount = 1;
    passInfo.pSubpasses = &subpass;
    passInfo.dependencyCount = 2;
    passInfo.pDependencies = deps;
    vkCheck(vkCreateRenderPass(device, &passInfo, nullptr, &blurRenderPass_), "vkCreateRenderPass (blur)");
}

void SsaoPass::createFramebuffers(VkDevice device) {
    VkImageView gbufferAttachments[3] = {gPosView_, gNormalView_, gDepthView_};
    VkFramebufferCreateInfo gbufferInfo{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    gbufferInfo.renderPass = gbufferRenderPass_;
    gbufferInfo.attachmentCount = 3;
    gbufferInfo.pAttachments = gbufferAttachments;
    gbufferInfo.width = (uint32_t)width_;
    gbufferInfo.height = (uint32_t)height_;
    gbufferInfo.layers = 1;
    vkCheck(vkCreateFramebuffer(device, &gbufferInfo, nullptr, &gbufferFramebuffer_),
            "vkCreateFramebuffer (gbuffer)");

    VkFramebufferCreateInfo ssaoInfo{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    ssaoInfo.renderPass = ssaoRenderPass_;
    ssaoInfo.attachmentCount = 1;
    ssaoInfo.pAttachments = &aoRawView_;
    ssaoInfo.width = (uint32_t)width_;
    ssaoInfo.height = (uint32_t)height_;
    ssaoInfo.layers = 1;
    vkCheck(vkCreateFramebuffer(device, &ssaoInfo, nullptr, &ssaoFramebuffer_), "vkCreateFramebuffer (ssao)");

    VkFramebufferCreateInfo blurInfo{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    blurInfo.renderPass = blurRenderPass_;
    blurInfo.attachmentCount = 1;
    blurInfo.pAttachments = &aoBlurredView_;
    blurInfo.width = (uint32_t)width_;
    blurInfo.height = (uint32_t)height_;
    blurInfo.layers = 1;
    vkCheck(vkCreateFramebuffer(device, &blurInfo, nullptr, &blurFramebuffer_), "vkCreateFramebuffer (blur)");
}

void SsaoPass::destroyFramebuffers(VkDevice device) {
    if (gbufferFramebuffer_) { vkDestroyFramebuffer(device, gbufferFramebuffer_, nullptr); gbufferFramebuffer_ = VK_NULL_HANDLE; }
    if (ssaoFramebuffer_) { vkDestroyFramebuffer(device, ssaoFramebuffer_, nullptr); ssaoFramebuffer_ = VK_NULL_HANDLE; }
    if (blurFramebuffer_) { vkDestroyFramebuffer(device, blurFramebuffer_, nullptr); blurFramebuffer_ = VK_NULL_HANDLE; }
}

void SsaoPass::createGBufferPipeline(VkDevice device, const std::filesystem::path& shaderDir) {
    VkShaderModule vert = createShaderModule(device, readFile(shaderDir / "gbuffer.vert.spv"));
    VkShaderModule frag = createShaderModule(device, readFile(shaderDir / "gbuffer.frag.spv"));
    VkPipelineShaderStageCreateInfo stages[2] = {shaderStage(VK_SHADER_STAGE_VERTEX_BIT, vert),
                                                  shaderStage(VK_SHADER_STAGE_FRAGMENT_BIT, frag)};

    VkVertexInputBindingDescription binding{};
    binding.binding = 0;
    binding.stride = sizeof(Vertex);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    // Mismo layout que LitPipeline: solo posicion + normal, el color de
    // Vertex no se usa en ninguna de las dos pipelines con shading real.
    VkVertexInputAttributeDescription attrs[2]{};
    attrs[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT, (uint32_t)offsetof(Vertex, position)};
    attrs[1] = {1, 0, VK_FORMAT_R32G32B32_SFLOAT, (uint32_t)offsetof(Vertex, normal)};

    VkPipelineVertexInputStateCreateInfo vertexInput{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &binding;
    vertexInput.vertexAttributeDescriptionCount = 2;
    vertexInput.pVertexAttributeDescriptions = attrs;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewportState{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = VK_CULL_MODE_NONE;
    raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisample{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;

    VkPipelineColorBlendAttachmentState blendAttachment{};
    blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                      VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    blendAttachment.blendEnable = VK_FALSE;
    std::array<VkPipelineColorBlendAttachmentState, 2> blendAttachments{blendAttachment, blendAttachment};

    VkPipelineColorBlendStateCreateInfo colorBlend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    colorBlend.attachmentCount = (uint32_t)blendAttachments.size();
    colorBlend.pAttachments = blendAttachments.data();

    VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicState{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamicState.dynamicStateCount = 2;
    dynamicState.pDynamicStates = dynamicStates;

    // Solo vertex shader necesita model/view/proj -- gbuffer.frag solo
    // escribe lo que ya le llega interpolado. 192 bytes, por debajo de los
    // 240 que ya exige/comprueba LitPipeline::create() para esta misma GPU.
    VkPushConstantRange pushConstant{};
    pushConstant.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushConstant.offset = 0;
    pushConstant.size = sizeof(PushConstantsGBuffer);

    VkPipelineLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushConstant;
    vkCheck(vkCreatePipelineLayout(device, &layoutInfo, nullptr, &gbufferLayout_),
            "vkCreatePipelineLayout (gbuffer)");

    VkGraphicsPipelineCreateInfo pipelineInfo{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = stages;
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &raster;
    pipelineInfo.pMultisampleState = &multisample;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlend;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = gbufferLayout_;
    pipelineInfo.renderPass = gbufferRenderPass_;
    pipelineInfo.subpass = 0;
    vkCheck(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &gbufferPipeline_),
            "vkCreateGraphicsPipelines (gbuffer)");

    vkDestroyShaderModule(device, vert, nullptr);
    vkDestroyShaderModule(device, frag, nullptr);
}

void SsaoPass::createSsaoPipeline(VkDevice device, const std::filesystem::path& shaderDir) {
    VkShaderModule vert = createShaderModule(device, readFile(shaderDir / "fullscreen.vert.spv"));
    VkShaderModule frag = createShaderModule(device, readFile(shaderDir / "ssao.frag.spv"));
    VkPipelineShaderStageCreateInfo stages[2] = {shaderStage(VK_SHADER_STAGE_VERTEX_BIT, vert),
                                                  shaderStage(VK_SHADER_STAGE_FRAGMENT_BIT, frag)};

    // Sin vertex buffer -- fullscreen.vert calcula gl_Position desde
    // gl_VertexIndex, se dibuja con vkCmdDraw(cmd, 3, 1, 0, 0).
    VkPipelineVertexInputStateCreateInfo vertexInput{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewportState{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = VK_CULL_MODE_NONE;
    raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisample{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};

    VkPipelineColorBlendAttachmentState blendAttachment{};
    blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT;
    blendAttachment.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo colorBlend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    colorBlend.attachmentCount = 1;
    colorBlend.pAttachments = &blendAttachment;

    VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicState{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamicState.dynamicStateCount = 2;
    dynamicState.pDynamicStates = dynamicStates;

    VkDescriptorSetLayoutBinding bindings[2]{};
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings[1] = bindings[0];
    bindings[1].binding = 1;

    VkDescriptorSetLayoutCreateInfo setLayoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    setLayoutInfo.bindingCount = 2;
    setLayoutInfo.pBindings = bindings;
    vkCheck(vkCreateDescriptorSetLayout(device, &setLayoutInfo, nullptr, &ssaoInputSetLayout_),
            "vkCreateDescriptorSetLayout (ssao input)");

    VkPushConstantRange pushConstant{};
    pushConstant.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    pushConstant.offset = 0;
    pushConstant.size = sizeof(PushConstantsSsao);

    VkPipelineLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &ssaoInputSetLayout_;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushConstant;
    vkCheck(vkCreatePipelineLayout(device, &layoutInfo, nullptr, &ssaoLayout_), "vkCreatePipelineLayout (ssao)");

    VkGraphicsPipelineCreateInfo pipelineInfo{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = stages;
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &raster;
    pipelineInfo.pMultisampleState = &multisample;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlend;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = ssaoLayout_;
    pipelineInfo.renderPass = ssaoRenderPass_;
    pipelineInfo.subpass = 0;
    vkCheck(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &ssaoPipeline_),
            "vkCreateGraphicsPipelines (ssao)");

    vkDestroyShaderModule(device, vert, nullptr);
    vkDestroyShaderModule(device, frag, nullptr);
}

void SsaoPass::createBlurPipeline(VkDevice device, const std::filesystem::path& shaderDir) {
    VkShaderModule vert = createShaderModule(device, readFile(shaderDir / "fullscreen.vert.spv"));
    VkShaderModule frag = createShaderModule(device, readFile(shaderDir / "blur.frag.spv"));
    VkPipelineShaderStageCreateInfo stages[2] = {shaderStage(VK_SHADER_STAGE_VERTEX_BIT, vert),
                                                  shaderStage(VK_SHADER_STAGE_FRAGMENT_BIT, frag)};

    VkPipelineVertexInputStateCreateInfo vertexInput{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewportState{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = VK_CULL_MODE_NONE;
    raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisample{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};

    VkPipelineColorBlendAttachmentState blendAttachment{};
    blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT;
    blendAttachment.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo colorBlend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    colorBlend.attachmentCount = 1;
    colorBlend.pAttachments = &blendAttachment;

    VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicState{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamicState.dynamicStateCount = 2;
    dynamicState.pDynamicStates = dynamicStates;

    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo setLayoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    setLayoutInfo.bindingCount = 1;
    setLayoutInfo.pBindings = &binding;
    vkCheck(vkCreateDescriptorSetLayout(device, &setLayoutInfo, nullptr, &blurInputSetLayout_),
            "vkCreateDescriptorSetLayout (blur input)");

    VkPipelineLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &blurInputSetLayout_;
    vkCheck(vkCreatePipelineLayout(device, &layoutInfo, nullptr, &blurLayout_), "vkCreatePipelineLayout (blur)");

    VkGraphicsPipelineCreateInfo pipelineInfo{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = stages;
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &raster;
    pipelineInfo.pMultisampleState = &multisample;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlend;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = blurLayout_;
    pipelineInfo.renderPass = blurRenderPass_;
    pipelineInfo.subpass = 0;
    vkCheck(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &blurPipeline_),
            "vkCreateGraphicsPipelines (blur)");

    vkDestroyShaderModule(device, vert, nullptr);
    vkDestroyShaderModule(device, frag, nullptr);
}

void SsaoPass::createDescriptors(VkDevice device) {
    // set de salida que consume LitPipeline (envuelve aoBlurredView_) --
    // su layout se crea aqui porque no hace falta una pipeline propia.
    VkDescriptorSetLayoutBinding outputBinding{};
    outputBinding.binding = 0;
    outputBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    outputBinding.descriptorCount = 1;
    outputBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo outputLayoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    outputLayoutInfo.bindingCount = 1;
    outputLayoutInfo.pBindings = &outputBinding;
    vkCheck(vkCreateDescriptorSetLayout(device, &outputLayoutInfo, nullptr, &aoOutputSetLayout_),
            "vkCreateDescriptorSetLayout (ao output)");

    // 4 combined-image-samplers en total: 2 (G-buffer) + 1 (aoRaw) + 1 (aoBlurred).
    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSize.descriptorCount = 4;
    VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    poolInfo.maxSets = 3;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    vkCheck(vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool_),
            "vkCreateDescriptorPool (SsaoPass)");

    auto allocateSet = [&](VkDescriptorSetLayout layout) {
        VkDescriptorSetAllocateInfo allocInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        allocInfo.descriptorPool = descriptorPool_;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &layout;
        VkDescriptorSet set = VK_NULL_HANDLE;
        vkCheck(vkAllocateDescriptorSets(device, &allocInfo, &set), "vkAllocateDescriptorSets (SsaoPass)");
        return set;
    };
    ssaoInputSet_ = allocateSet(ssaoInputSetLayout_);
    blurInputSet_ = allocateSet(blurInputSetLayout_);
    aoOutputSet_ = allocateSet(aoOutputSetLayout_);
}

void SsaoPass::writeDescriptors(VkDevice device) {
    VkDescriptorImageInfo gPosInfo{linearSampler_, gPosView_, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkDescriptorImageInfo gNormalInfo{linearSampler_, gNormalView_, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkDescriptorImageInfo aoRawInfo{linearSampler_, aoRawView_, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkDescriptorImageInfo aoBlurredInfo{linearSampler_, aoBlurredView_, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};

    VkWriteDescriptorSet writes[4]{};
    writes[0] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    writes[0].dstSet = ssaoInputSet_;
    writes[0].dstBinding = 0;
    writes[0].descriptorCount = 1;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[0].pImageInfo = &gPosInfo;

    writes[1] = writes[0];
    writes[1].dstBinding = 1;
    writes[1].pImageInfo = &gNormalInfo;

    writes[2] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    writes[2].dstSet = blurInputSet_;
    writes[2].dstBinding = 0;
    writes[2].descriptorCount = 1;
    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[2].pImageInfo = &aoRawInfo;

    writes[3] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    writes[3].dstSet = aoOutputSet_;
    writes[3].dstBinding = 0;
    writes[3].descriptorCount = 1;
    writes[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[3].pImageInfo = &aoBlurredInfo;

    vkUpdateDescriptorSets(device, 4, writes, 0, nullptr);
}

void SsaoPass::beginGBufferPass(VkCommandBuffer cmd, const glm::mat4& view, const glm::mat4& proj) {
    gbufferView_ = view;
    gbufferProj_ = proj;

    VkClearValue clears[3]{};
    clears[0].color = {{0.0f, 0.0f, 0.0f, 0.0f}};
    clears[1].color = {{0.0f, 0.0f, 0.0f, 0.0f}}; // normal a cero -- sentinel "sin geometria" que lee ssao.frag
    clears[2].depthStencil = {1.0f, 0};

    VkRenderPassBeginInfo passBegin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    passBegin.renderPass = gbufferRenderPass_;
    passBegin.framebuffer = gbufferFramebuffer_;
    passBegin.renderArea = {{0, 0}, {(uint32_t)width_, (uint32_t)height_}};
    passBegin.clearValueCount = 3;
    passBegin.pClearValues = clears;
    vkCmdBeginRenderPass(cmd, &passBegin, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport viewport{0.0f, 0.0f, (float)width_, (float)height_, 0.0f, 1.0f};
    VkRect2D scissor{{0, 0}, {(uint32_t)width_, (uint32_t)height_}};
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, gbufferPipeline_);
}

void SsaoPass::drawMeshGBuffer(VkCommandBuffer cmd, const glm::mat4& model) const {
    PushConstantsGBuffer pc{model, gbufferView_, gbufferProj_};
    vkCmdPushConstants(cmd, gbufferLayout_, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushConstantsGBuffer), &pc);
}

void SsaoPass::endGBufferPass(VkCommandBuffer cmd) {
    vkCmdEndRenderPass(cmd);

    VkViewport viewport{0.0f, 0.0f, (float)width_, (float)height_, 0.0f, 1.0f};
    VkRect2D scissor{{0, 0}, {(uint32_t)width_, (uint32_t)height_}};

    // Pasada de SSAO: lee el G-buffer que acaba de cerrarse, escribe aoRaw.
    VkClearValue ssaoClear{};
    ssaoClear.color = {{1.0f, 0.0f, 0.0f, 0.0f}};
    VkRenderPassBeginInfo ssaoBegin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    ssaoBegin.renderPass = ssaoRenderPass_;
    ssaoBegin.framebuffer = ssaoFramebuffer_;
    ssaoBegin.renderArea = {{0, 0}, {(uint32_t)width_, (uint32_t)height_}};
    ssaoBegin.clearValueCount = 1;
    ssaoBegin.pClearValues = &ssaoClear;
    vkCmdBeginRenderPass(cmd, &ssaoBegin, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, ssaoPipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, ssaoLayout_, 0, 1, &ssaoInputSet_, 0, nullptr);
    PushConstantsSsao ssaoPc{gbufferProj_, glm::vec4(radius, bias, power, 0.0f)};
    vkCmdPushConstants(cmd, ssaoLayout_, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstantsSsao), &ssaoPc);
    vkCmdDraw(cmd, 3, 1, 0, 0);
    vkCmdEndRenderPass(cmd);

    // Pasada de blur: lee aoRaw que acaba de cerrarse, escribe aoBlurred
    // (el resultado final que descriptorSet()/descriptorSetLayout() expone).
    VkClearValue blurClear{};
    blurClear.color = {{1.0f, 0.0f, 0.0f, 0.0f}};
    VkRenderPassBeginInfo blurBegin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    blurBegin.renderPass = blurRenderPass_;
    blurBegin.framebuffer = blurFramebuffer_;
    blurBegin.renderArea = {{0, 0}, {(uint32_t)width_, (uint32_t)height_}};
    blurBegin.clearValueCount = 1;
    blurBegin.pClearValues = &blurClear;
    vkCmdBeginRenderPass(cmd, &blurBegin, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, blurPipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, blurLayout_, 0, 1, &blurInputSet_, 0, nullptr);
    vkCmdDraw(cmd, 3, 1, 0, 0);
    vkCmdEndRenderPass(cmd);
}

} // namespace iron3d
