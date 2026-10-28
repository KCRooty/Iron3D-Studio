#include "GizmoMesh.h"
#include "../gfx/VulkanContext.h"

#include <cstring>
#include <algorithm>

namespace iron3d {

void GizmoMesh::createBuffer(VmaAllocator allocator, uint32_t maxVertices, DynamicBuffer& out) {
    VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bufferInfo.size = sizeof(Vertex) * (VkDeviceSize)maxVertices;
    bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                       VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VmaAllocationInfo info{};
    vkCheck(vmaCreateBuffer(allocator, &bufferInfo, &allocInfo, &out.buffer, &out.allocation, &info),
            "vmaCreateBuffer (GizmoMesh)");
    out.mapped = info.pMappedData;
    out.capacity = maxVertices;
    out.count = 0;
}

void GizmoMesh::destroyBuffer(VmaAllocator allocator, DynamicBuffer& buf) {
    if (buf.buffer) vmaDestroyBuffer(allocator, buf.buffer, buf.allocation);
    buf = DynamicBuffer{};
}

void GizmoMesh::writeBuffer(DynamicBuffer& buf, const std::vector<Vertex>& verts) {
    const uint32_t n = (uint32_t)std::min<size_t>(verts.size(), buf.capacity);
    if (n > 0) std::memcpy(buf.mapped, verts.data(), sizeof(Vertex) * n);
    buf.count = n;
}

void GizmoMesh::create(VmaAllocator allocator, uint32_t maxVertices) {
    createBuffer(allocator, maxVertices, lines_);
    createBuffer(allocator, maxVertices, triangles_);
}

void GizmoMesh::destroy(VmaAllocator allocator) {
    destroyBuffer(allocator, lines_);
    destroyBuffer(allocator, triangles_);
}

void GizmoMesh::upload(const std::vector<Vertex>& lineVerts, const std::vector<Vertex>& triVerts) {
    writeBuffer(lines_, lineVerts);
    writeBuffer(triangles_, triVerts);
}

void GizmoMesh::drawLines(VkCommandBuffer cmd) const {
    if (lines_.count == 0) return;
    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &lines_.buffer, &offset);
    vkCmdDraw(cmd, lines_.count, 1, 0, 0);
}

void GizmoMesh::drawTriangles(VkCommandBuffer cmd) const {
    if (triangles_.count == 0) return;
    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &triangles_.buffer, &offset);
    vkCmdDraw(cmd, triangles_.count, 1, 0, 0);
}

} // namespace iron3d
