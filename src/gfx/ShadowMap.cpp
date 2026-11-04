#include "ShadowMap.h"
#include "VulkanContext.h"

#include <cstdio>
#include <cstdlib>

namespace iron3d {

namespace {

VkFormat pickShadowFormat(VkPhysicalDevice physicalDevice) {
    // A diferencia del buffer de profundidad del swapchain (SwapchainWindow,
    // solo attachment), este TAMBIEN hay que poder muestrearlo desde
    // lit.frag -- se exige SAMPLED_IMAGE_BIT ademas de
    // DEPTH_STENCIL_ATTACHMENT_BIT. D32_SFLOAT con ambas features es
    // practicamente universal en GPU de escritorio.
    const VkFormat candidates[] = {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D24_UNORM_S8_UINT};
    const VkFormatFeatureFlags needed =
        VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
    for (VkFormat format : candidates) {
        VkFormatProperties props{};
        vkGetPhysicalDeviceFormatProperties(physicalDevice, format, &props);
        if ((props.optimalTilingFeatures & needed) == needed) return format;
    }
    std::fprintf(stderr,
                 "[Iron3D] Ninguna GPU deberia llegar aqui -- sin formato de profundidad "
                 "muestreable valido para el mapa de sombras.\n");
    std::abort();
}

} // namespace

void ShadowMap::create(VkDevice device, VkPhysicalDevice physicalDevice, VmaAllocator allocator,
                        const std::filesystem::path& shaderDir) {
    format_ = pickShadowFormat(physicalDevice);

    VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = format_;
    imageInfo.extent = {kResolution, kResolution, 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO; // device-local: sin flags de acceso desde CPU

    vkCheck(vmaCreateImage(allocator, &imageInfo, &allocInfo, &image_, &allocation_, nullptr),
            "vmaCreateImage (ShadowMap)");

    const bool hasStencil = format_ == VK_FORMAT_D24_UNORM_S8_UINT;
    VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    viewInfo.image = image_;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format_;
    // Solo el aspecto de profundidad: aunque el formato traiga stencil, es
    // lo unico que lit.frag necesita leer.
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.layerCount = 1;
    (void)hasStencil;
    vkCheck(vkCreateImageView(device, &viewInfo, nullptr, &view_), "vkCreateImageView (ShadowMap)");

    // NEAREST + PCF manual en el shader (ver shadowFactor() en lit.frag), no
    // un sampler de comparacion por hardware -- mas simple de razonar y
    // suficiente para esta fase. CLAMP_TO_BORDER con borde blanco
    // (profundidad 1.0 = "lejos") para que todo lo que caiga fuera del
    // frustum ortografico de la luz simplemente no este en sombra.
    VkSamplerCreateInfo samplerInfo{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    samplerInfo.magFilter = VK_FILTER_NEAREST;
    samplerInfo.minFilter = VK_FILTER_NEAREST;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    samplerInfo.maxLod = 1.0f;
    vkCheck(vkCreateSampler(device, &samplerInfo, nullptr, &sampler_), "vkCreateSampler (ShadowMap)");

    // Render pass propio de un solo attachment de profundidad. loadOp=CLEAR
    // (cada frame se vuelve a dibujar desde cero), storeOp=STORE (al
    // contrario que el depth buffer del swapchain, a ESTE si hay que
    // leerlo despues). finalLayout=SHADER_READ_ONLY_OPTIMAL: la transicion
    // de layout la hace el propio render pass al cerrar (vkCmdEndRenderPass),
    // sin barrera manual aparte -- las dependencias de abajo son las que
    // hacen that transition (y el orden lectura-de-la-pasada-anterior antes
    // de escritura-de-esta) seguras.
    VkAttachmentDescription attachment{};
    attachment.format = format_;
    attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkAttachmentReference depthRef{0, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.pDepthStencilAttachment = &depthRef;

    VkSubpassDependency dependencies[2]{};
    // Antes de escribir: esperar a que la pasada principal del FRAME
    // ANTERIOR haya terminado de muestrear este mapa (si no, se pisaria la
    // lectura en marcha).
    dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[0].dstSubpass = 0;
    dependencies[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependencies[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    dependencies[0].dstStageMask =
        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dependencies[0].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    // Despues de escribir: la pasada principal de ESTE frame no puede
    // muestrear hasta que la escritura de profundidad haya terminado.
    dependencies[1].srcSubpass = 0;
    dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[1].srcStageMask = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dependencies[1].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    VkRenderPassCreateInfo rpInfo{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    rpInfo.attachmentCount = 1;
    rpInfo.pAttachments = &attachment;
    rpInfo.subpassCount = 1;
    rpInfo.pSubpasses = &subpass;
    rpInfo.dependencyCount = 2;
    rpInfo.pDependencies = dependencies;
    vkCheck(vkCreateRenderPass(device, &rpInfo, nullptr, &renderPass_), "vkCreateRenderPass (ShadowMap)");

    VkFramebufferCreateInfo fbInfo{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    fbInfo.renderPass = renderPass_;
    fbInfo.attachmentCount = 1;
    fbInfo.pAttachments = &view_;
    fbInfo.width = kResolution;
    fbInfo.height = kResolution;
    fbInfo.layers = 1;
    vkCheck(vkCreateFramebuffer(device, &fbInfo, nullptr, &framebuffer_), "vkCreateFramebuffer (ShadowMap)");

    // Descriptor set layout/pool/set: un unico combined-image-sampler en el
    // binding 0, leido solo desde el fragment shader. La vista/sampler no
    // cambian nunca tras create() (la resolucion es fija, independiente del
    // tamano de ventana), asi que el set se escribe una sola vez aqui.
    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo setLayoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    setLayoutInfo.bindingCount = 1;
    setLayoutInfo.pBindings = &binding;
    vkCheck(vkCreateDescriptorSetLayout(device, &setLayoutInfo, nullptr, &descriptorSetLayout_),
            "vkCreateDescriptorSetLayout (ShadowMap)");

    VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1};
    VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    poolInfo.maxSets = 1;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    vkCheck(vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool_),
            "vkCreateDescriptorPool (ShadowMap)");

    VkDescriptorSetAllocateInfo allocSetInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    allocSetInfo.descriptorPool = descriptorPool_;
    allocSetInfo.descriptorSetCount = 1;
    allocSetInfo.pSetLayouts = &descriptorSetLayout_;
    vkCheck(vkAllocateDescriptorSets(device, &allocSetInfo, &descriptorSet_),
            "vkAllocateDescriptorSets (ShadowMap)");

    VkDescriptorImageInfo imageDescInfo{};
    imageDescInfo.sampler = sampler_;
    imageDescInfo.imageView = view_;
    imageDescInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = descriptorSet_;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &imageDescInfo;
    vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);

    pipeline_.create(device, renderPass_, shaderDir);
}

void ShadowMap::destroy(VkDevice device, VmaAllocator allocator) {
    pipeline_.destroy(device);
    if (descriptorPool_) vkDestroyDescriptorPool(device, descriptorPool_, nullptr);
    if (descriptorSetLayout_) vkDestroyDescriptorSetLayout(device, descriptorSetLayout_, nullptr);
    if (framebuffer_) vkDestroyFramebuffer(device, framebuffer_, nullptr);
    if (renderPass_) vkDestroyRenderPass(device, renderPass_, nullptr);
    if (sampler_) vkDestroySampler(device, sampler_, nullptr);
    if (view_) vkDestroyImageView(device, view_, nullptr);
    if (image_) vmaDestroyImage(allocator, image_, allocation_);
    descriptorPool_ = VK_NULL_HANDLE;
    descriptorSetLayout_ = VK_NULL_HANDLE;
    framebuffer_ = VK_NULL_HANDLE;
    renderPass_ = VK_NULL_HANDLE;
    sampler_ = VK_NULL_HANDLE;
    view_ = VK_NULL_HANDLE;
    image_ = VK_NULL_HANDLE;
}

void ShadowMap::beginPass(VkCommandBuffer cmd) const {
    VkClearValue clearValue{};
    clearValue.depthStencil = {1.0f, 0};

    VkRenderPassBeginInfo rpInfo{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    rpInfo.renderPass = renderPass_;
    rpInfo.framebuffer = framebuffer_;
    rpInfo.renderArea.extent = {kResolution, kResolution};
    rpInfo.clearValueCount = 1;
    rpInfo.pClearValues = &clearValue;
    vkCmdBeginRenderPass(cmd, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport viewport{0.0f, 0.0f, (float)kResolution, (float)kResolution, 0.0f, 1.0f};
    VkRect2D scissor{{0, 0}, {kResolution, kResolution}};
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    pipeline_.bind(cmd);
}

void ShadowMap::pushLightMvp(VkCommandBuffer cmd, const glm::mat4& lightMvp) const {
    pipeline_.pushLightMvp(cmd, lightMvp);
}

void ShadowMap::endPass(VkCommandBuffer cmd) const {
    vkCmdEndRenderPass(cmd);
}

} // namespace iron3d
