#include "PrimitiveMesh.h"
#include "../gfx/VulkanContext.h"

#include <cstring>

namespace iron3d {

namespace {

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
            "vmaCreateBuffer (PrimitiveMesh)");
    std::memcpy(info.pMappedData, data, (size_t)size);
    return buffer;
}

} // namespace

void PrimitiveMesh::create(VkDevice /*device*/, VmaAllocator allocator, const MeshData& data) {
    vertexBuffer_ = createHostVisibleBuffer(
        allocator, sizeof(Vertex) * data.vertices.size(), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        vertexAllocation_, data.vertices.data());
    indexBuffer_ = createHostVisibleBuffer(
        allocator, sizeof(uint16_t) * data.indices.size(), VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        indexAllocation_, data.indices.data());
    indexCount_ = (uint32_t)data.indices.size();
}

void PrimitiveMesh::destroy(VkDevice /*device*/, VmaAllocator allocator) {
    if (vertexBuffer_) vmaDestroyBuffer(allocator, vertexBuffer_, vertexAllocation_);
    if (indexBuffer_) vmaDestroyBuffer(allocator, indexBuffer_, indexAllocation_);
    vertexBuffer_ = indexBuffer_ = VK_NULL_HANDLE;
}

void PrimitiveMesh::draw(VkCommandBuffer cmd) const {
    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &vertexBuffer_, &offset);
    vkCmdBindIndexBuffer(cmd, indexBuffer_, 0, VK_INDEX_TYPE_UINT16);
    vkCmdDrawIndexed(cmd, indexCount_, 1, 0, 0, 0);
}

} // namespace iron3d
