#include "LightsUniform.h"
#include "VulkanContext.h"

#include <algorithm>

namespace iron3d {

static_assert(sizeof(GpuLight) == 64, "GpuLight: 4 vec4 (std140)");
static_assert(sizeof(GpuSceneLights) <= 65536, "vkCmdUpdateBuffer admite hasta 64 KB");
static_assert(sizeof(GpuSceneLights) % 4 == 0, "vkCmdUpdateBuffer necesita multiplos de 4");

GpuSceneLights packSceneLights(const SceneLighting& lighting, const glm::mat4& lightViewProj) {
    GpuSceneLights g{};
    g.lightViewProj = lightViewProj;
    const int n = std::min((int)lighting.lights.size(), kMaxViewportLights);
    g.info = glm::vec4((float)n, (float)lighting.shadowLight, n > 0 ? kAmbientSceneLights : kAmbientDefaultLight,
                       0.0f);
    for (int i = 0; i < n; ++i) {
        const ViewportLight& l = lighting.lights[i];
        g.lights[i].posType = glm::vec4(l.position, (float)l.type);
        g.lights[i].dirFalloff = glm::vec4(l.direction, (float)l.falloff);
        g.lights[i].colorRadius = glm::vec4(l.color, l.radius);
        g.lights[i].cone = glm::vec4(l.cosOuter, l.cosInner, l.innerRadius, (float)l.shaderFlags());
    }
    return g;
}

void LightsUniform::create(VkDevice device, VmaAllocator allocator) {
    VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bi.size = sizeof(GpuSceneLights);
    bi.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    VmaAllocationCreateInfo ai{};
    ai.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    vkCheck(vmaCreateBuffer(allocator, &bi, &ai, &buffer_, &alloc_, nullptr), "vmaCreateBuffer (luces)");

    VkDescriptorSetLayoutBinding b{};
    b.binding = 0;
    b.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    b.descriptorCount = 1;
    b.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo li{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    li.bindingCount = 1;
    li.pBindings = &b;
    vkCheck(vkCreateDescriptorSetLayout(device, &li, nullptr, &setLayout_), "vkCreateDescriptorSetLayout (luces)");

    VkDescriptorPoolSize ps{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1};
    VkDescriptorPoolCreateInfo dpi{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    dpi.maxSets = 1;
    dpi.poolSizeCount = 1;
    dpi.pPoolSizes = &ps;
    vkCheck(vkCreateDescriptorPool(device, &dpi, nullptr, &pool_), "vkCreateDescriptorPool (luces)");

    VkDescriptorSetAllocateInfo sai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    sai.descriptorPool = pool_;
    sai.descriptorSetCount = 1;
    sai.pSetLayouts = &setLayout_;
    vkCheck(vkAllocateDescriptorSets(device, &sai, &set_), "vkAllocateDescriptorSets (luces)");

    VkDescriptorBufferInfo dbi{buffer_, 0, sizeof(GpuSceneLights)};
    VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    w.dstSet = set_;
    w.dstBinding = 0;
    w.descriptorCount = 1;
    w.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    w.pBufferInfo = &dbi;
    vkUpdateDescriptorSets(device, 1, &w, 0, nullptr);
}

void LightsUniform::destroy(VkDevice device, VmaAllocator allocator) {
    if (pool_) vkDestroyDescriptorPool(device, pool_, nullptr);
    if (setLayout_) vkDestroyDescriptorSetLayout(device, setLayout_, nullptr);
    if (buffer_) vmaDestroyBuffer(allocator, buffer_, alloc_);
    pool_ = VK_NULL_HANDLE;
    setLayout_ = VK_NULL_HANDLE;
    buffer_ = VK_NULL_HANDLE;
    alloc_ = VK_NULL_HANDLE;
    set_ = VK_NULL_HANDLE;
}

void LightsUniform::update(VkCommandBuffer cmd, const GpuSceneLights& data) {
    // Las lecturas del frame anterior (vertex/fragment) terminan antes de escribir...
    VkBufferMemoryBarrier before{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
    before.srcAccessMask = VK_ACCESS_UNIFORM_READ_BIT;
    before.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    before.srcQueueFamilyIndex = before.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    before.buffer = buffer_;
    before.size = VK_WHOLE_SIZE;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 1, &before, 0, nullptr);
    vkCmdUpdateBuffer(cmd, buffer_, 0, sizeof(GpuSceneLights), &data);
    // ...y las de este frame esperan a la escritura.
    VkBufferMemoryBarrier after = before;
    after.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    after.dstAccessMask = VK_ACCESS_UNIFORM_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 1,
                         &after, 0, nullptr);
}

} // namespace iron3d
