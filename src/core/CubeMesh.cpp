#include "CubeMesh.h"

#include "../gfx/VulkanContext.h"

#include <array>
#include <cstring>

namespace iron3d {

namespace {

// 8 vertices, un color por esquina (sin normales -- Fase 1 es unlit).
const std::array<Vertex, 8> kVertices = {{
    {{-0.5f, -0.5f, -0.5f}, {0.9f, 0.2f, 0.2f}},
    {{ 0.5f, -0.5f, -0.5f}, {0.2f, 0.9f, 0.2f}},
    {{ 0.5f,  0.5f, -0.5f}, {0.2f, 0.2f, 0.9f}},
    {{-0.5f,  0.5f, -0.5f}, {0.9f, 0.9f, 0.2f}},
    {{-0.5f, -0.5f,  0.5f}, {0.9f, 0.2f, 0.9f}},
    {{ 0.5f, -0.5f,  0.5f}, {0.2f, 0.9f, 0.9f}},
    {{ 0.5f,  0.5f,  0.5f}, {0.95f, 0.95f, 0.95f}},
    {{-0.5f,  0.5f,  0.5f}, {0.15f, 0.15f, 0.15f}},
}};

// 12 triangulos, CCW visto desde fuera (coherente con VK_CULL_MODE_BACK_BIT en BasicPipeline).
const std::array<uint16_t, 36> kIndices = {{
    0, 1, 2,  2, 3, 0,   // trasera  (-Z)
    5, 4, 7,  7, 6, 5,   // frontal  (+Z)
    4, 0, 3,  3, 7, 4,   // izquierda (-X)
    1, 5, 6,  6, 2, 1,   // derecha   (+X)
    3, 2, 6,  6, 7, 3,   // arriba    (+Y)
    4, 5, 1,  1, 0, 4,   // abajo     (-Y)
}};

VkBuffer createHostVisibleBuffer(VmaAllocator allocator, VkDeviceSize size,
                                  VkBufferUsageFlags usage, VmaAllocation& outAllocation,
                                  const void* data) {
    VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                       VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VkBuffer buffer = VK_NULL_HANDLE;
    VmaAllocationInfo info{};
    vkCheck(vmaCreateBuffer(allocator, &bufferInfo, &allocInfo, &buffer, &outAllocation, &info),
            "vmaCreateBuffer (CubeMesh)");
    std::memcpy(info.pMappedData, data, (size_t)size);
    return buffer;
}

} // namespace

void CubeMesh::create(VkDevice /*device*/, VmaAllocator allocator) {
    vertexBuffer_ = createHostVisibleBuffer(
        allocator, sizeof(kVertices), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        vertexAllocation_, kVertices.data());
    indexBuffer_ = createHostVisibleBuffer(
        allocator, sizeof(kIndices), VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        indexAllocation_, kIndices.data());
    indexCount_ = (uint32_t)kIndices.size();
}

void CubeMesh::destroy(VkDevice /*device*/, VmaAllocator allocator) {
    if (vertexBuffer_) vmaDestroyBuffer(allocator, vertexBuffer_, vertexAllocation_);
    if (indexBuffer_) vmaDestroyBuffer(allocator, indexBuffer_, indexAllocation_);
    vertexBuffer_ = indexBuffer_ = VK_NULL_HANDLE;
}

void CubeMesh::draw(VkCommandBuffer cmd) const {
    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &vertexBuffer_, &offset);
    vkCmdBindIndexBuffer(cmd, indexBuffer_, 0, VK_INDEX_TYPE_UINT16);
    vkCmdDrawIndexed(cmd, indexCount_, 1, 0, 0, 0);
}

} // namespace iron3d
