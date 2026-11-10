#include "MaterialTextures.h"
#include "VulkanContext.h"
#include "core/Material.h"
#include "core/TextureImage.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace iron3d {

namespace {
// Tope de descriptores: de sobra para los modelos de FNAF mas grandes que hay
// en disco (la libreria con mas texturas trae ~290). Si se acaba, las
// texturas siguientes se ven con el color plano del material.
constexpr uint32_t kMaxTextures = 2048;
constexpr VkFormat kFormat = VK_FORMAT_R8G8B8A8_SRGB;
} // namespace

void MaterialTextures::create(VkDevice device, VkPhysicalDevice physicalDevice, VmaAllocator allocator,
                              VkQueue queue, uint32_t queueFamily) {
    device_ = device;
    allocator_ = allocator;
    queue_ = queue;

    VkFormatProperties fp{};
    vkGetPhysicalDeviceFormatProperties(physicalDevice, kFormat, &fp);
    const VkFormatFeatureFlags need = VK_FORMAT_FEATURE_BLIT_SRC_BIT | VK_FORMAT_FEATURE_BLIT_DST_BIT |
                                      VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
    canBlitMips_ = (fp.optimalTilingFeatures & need) == need;

    VkCommandPoolCreateInfo pi{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pi.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    pi.queueFamilyIndex = queueFamily;
    vkCheck(vkCreateCommandPool(device, &pi, nullptr, &pool_), "vkCreateCommandPool (texturas)");

    VkDescriptorSetLayoutBinding b{};
    b.binding = 0;
    b.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    b.descriptorCount = 1;
    b.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo li{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    li.bindingCount = 1;
    li.pBindings = &b;
    vkCheck(vkCreateDescriptorSetLayout(device, &li, nullptr, &setLayout_), "vkCreateDescriptorSetLayout (texturas)");

    VkDescriptorPoolSize ps{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, kMaxTextures + 1};
    VkDescriptorPoolCreateInfo dpi{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    dpi.maxSets = kMaxTextures + 1;
    dpi.poolSizeCount = 1;
    dpi.pPoolSizes = &ps;
    vkCheck(vkCreateDescriptorPool(device, &dpi, nullptr, &descriptorPool_), "vkCreateDescriptorPool (texturas)");

    // Mosaico = REPEAT (la Etiqueta de Textura sin mosaico lo resuelve el
    // shader); mipmaps lineales para que una textura grande lejos no centellee.
    VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    si.magFilter = VK_FILTER_LINEAR;
    si.minFilter = VK_FILTER_LINEAR;
    si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    si.addressModeU = si.addressModeV = si.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    si.maxLod = VK_LOD_CLAMP_NONE;
    vkCheck(vkCreateSampler(device, &si, nullptr, &sampler_), "vkCreateSampler (texturas)");

    const uint8_t white[4] = {255, 255, 255, 255};
    if (!upload(white, 1, 1, white_)) {
        std::fprintf(stderr, "[Iron3D] No se pudo crear la textura blanca por defecto.\n");
        std::abort();
    }
}

void MaterialTextures::destroy(VkDevice device, VmaAllocator allocator) {
    (void)device;
    (void)allocator;
    for (const Texture& t : textures_) freeTexture(t);
    textures_.clear();
    cache_.clear();
    freeTexture(white_);
    white_ = {};
    if (sampler_) vkDestroySampler(device_, sampler_, nullptr);
    if (descriptorPool_) vkDestroyDescriptorPool(device_, descriptorPool_, nullptr); // libera los sets
    if (setLayout_) vkDestroyDescriptorSetLayout(device_, setLayout_, nullptr);
    if (pool_) vkDestroyCommandPool(device_, pool_, nullptr);
    sampler_ = VK_NULL_HANDLE;
    descriptorPool_ = VK_NULL_HANDLE;
    setLayout_ = VK_NULL_HANDLE;
    pool_ = VK_NULL_HANDLE;
}

VkDescriptorSet MaterialTextures::get(const std::string& rawPath, const std::string& baseDir) {
    if (rawPath.empty()) return VK_NULL_HANDLE;
    const std::string key = textureCacheKey(rawPath, baseDir);
    auto it = cache_.find(key);
    if (it != cache_.end()) return it->second < 0 ? VK_NULL_HANDLE : textures_[(size_t)it->second].set;

    int index = -1;
    const std::string path = resolveTexturePath(rawPath, baseDir);
    TextureImage img;
    if (path.empty()) {
        std::fprintf(stderr, "[Iron3D] Textura no encontrada: %s\n", rawPath.c_str());
    } else if (!loadTextureImage(path, img)) {
        std::fprintf(stderr, "[Iron3D] Formato de textura no soportado o archivo roto: %s\n", path.c_str());
    } else if (textures_.size() >= kMaxTextures) {
        std::fprintf(stderr, "[Iron3D] Tope de %u texturas alcanzado; %s se ve sin textura.\n", kMaxTextures,
                     path.c_str());
    } else {
        Texture t;
        if (upload(img.rgba.data(), img.width, img.height, t)) {
            textures_.push_back(t);
            index = (int)textures_.size() - 1;
        }
    }
    cache_.emplace(key, index);
    return index < 0 ? VK_NULL_HANDLE : textures_[(size_t)index].set;
}

// Subida sincrona (buffer de paso + vkQueueWaitIdle), como createUiTexture:
// solo pasa la primera vez que aparece cada textura.
bool MaterialTextures::upload(const uint8_t* rgba, int width, int height, Texture& t) {
    const uint32_t levels =
        canBlitMips_ ? (uint32_t)std::floor(std::log2((double)std::max(width, height))) + 1u : 1u;

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
    if (vmaCreateBuffer(allocator_, &bi, &bai, &staging, &stagingAlloc, &stagingInfo) != VK_SUCCESS) return false;
    std::memcpy(stagingInfo.pMappedData, rgba, size_t(bytes));
    vmaFlushAllocation(allocator_, stagingAlloc, 0, VK_WHOLE_SIZE);

    VkImageCreateInfo ii{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ii.imageType = VK_IMAGE_TYPE_2D;
    ii.format = kFormat;
    ii.extent = {uint32_t(width), uint32_t(height), 1};
    ii.mipLevels = levels;
    ii.arrayLayers = 1;
    ii.samples = VK_SAMPLE_COUNT_1_BIT;
    ii.tiling = VK_IMAGE_TILING_OPTIMAL;
    ii.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VmaAllocationCreateInfo iai{};
    iai.usage = VMA_MEMORY_USAGE_AUTO;
    if (vmaCreateImage(allocator_, &ii, &iai, &t.image, &t.alloc, nullptr) != VK_SUCCESS) {
        vmaDestroyBuffer(allocator_, staging, stagingAlloc);
        t = {};
        return false;
    }

    VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    ai.commandPool = pool_;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    vkCheck(vkAllocateCommandBuffers(device_, &ai, &cmd), "vkAllocateCommandBuffers (texturas)");
    VkCommandBufferBeginInfo cbi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    cbi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &cbi);

    auto barrier = [&](uint32_t mip, uint32_t count, VkImageLayout from, VkImageLayout to, VkAccessFlags src,
                       VkAccessFlags dst, VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage) {
        VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image = t.image;
        b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, mip, count, 0, 1};
        b.oldLayout = from;
        b.newLayout = to;
        b.srcAccessMask = src;
        b.dstAccessMask = dst;
        vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &b);
    };

    barrier(0, levels, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0,
            VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent = ii.extent;
    vkCmdCopyBufferToImage(cmd, staging, t.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    // Cadena de mipmaps: cada nivel sale del anterior a mitad de tamano.
    int32_t mw = width, mh = height;
    for (uint32_t i = 1; i < levels; ++i) {
        barrier(i - 1, 1, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT);
        const int32_t nw = std::max(mw / 2, 1), nh = std::max(mh / 2, 1);
        VkImageBlit blit{};
        blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, i - 1, 0, 1};
        blit.srcOffsets[1] = {mw, mh, 1};
        blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, i, 0, 1};
        blit.dstOffsets[1] = {nw, nh, 1};
        vkCmdBlitImage(cmd, t.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, t.image,
                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);
        barrier(i - 1, 1, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
        mw = nw;
        mh = nh;
    }
    barrier(levels - 1, 1, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

    vkEndCommandBuffer(cmd);
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cmd;
    vkCheck(vkQueueSubmit(queue_, 1, &submit, VK_NULL_HANDLE), "vkQueueSubmit (texturas)");
    vkCheck(vkQueueWaitIdle(queue_), "vkQueueWaitIdle (texturas)");
    vkFreeCommandBuffers(device_, pool_, 1, &cmd);
    vmaDestroyBuffer(allocator_, staging, stagingAlloc);

    VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vi.image = t.image;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = kFormat;
    vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, levels, 0, 1};
    vkCheck(vkCreateImageView(device_, &vi, nullptr, &t.view), "vkCreateImageView (texturas)");

    VkDescriptorSetAllocateInfo dai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    dai.descriptorPool = descriptorPool_;
    dai.descriptorSetCount = 1;
    dai.pSetLayouts = &setLayout_;
    if (vkAllocateDescriptorSets(device_, &dai, &t.set) != VK_SUCCESS) {
        freeTexture(t);
        t = {};
        return false;
    }
    VkDescriptorImageInfo info{sampler_, t.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    w.dstSet = t.set;
    w.dstBinding = 0;
    w.descriptorCount = 1;
    w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    w.pImageInfo = &info;
    vkUpdateDescriptorSets(device_, 1, &w, 0, nullptr);
    return true;
}

void MaterialTextures::freeTexture(const Texture& t) {
    // El set vuelve al pool al destruir el pool (no se crea con FREE_DESCRIPTOR_SET).
    if (t.view) vkDestroyImageView(device_, t.view, nullptr);
    if (t.image) vmaDestroyImage(allocator_, t.image, t.alloc);
}

} // namespace iron3d
