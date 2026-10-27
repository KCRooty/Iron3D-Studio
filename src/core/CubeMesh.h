#pragma once

// Malla basica (Fase 1, Documento Maestro §8.1 punto 1): un cubo con buffers
// de vertices/indices en VRAM via VMA. Sin normales/PBR todavia -- solo color
// por vertice, para confirmar visualmente que geometria + camara + pipeline
// funcionan. El clonador real en GPU (IronCompute) es Fase 3.

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>

namespace iron3d {

struct Vertex {
    glm::vec3 position;
    glm::vec3 color;
};

class CubeMesh {
public:
    void create(VkDevice device, VmaAllocator allocator);
    void destroy(VkDevice device, VmaAllocator allocator);
    void draw(VkCommandBuffer cmd) const;

private:
    VkBuffer vertexBuffer_ = VK_NULL_HANDLE;
    VmaAllocation vertexAllocation_ = VK_NULL_HANDLE;
    VkBuffer indexBuffer_ = VK_NULL_HANDLE;
    VmaAllocation indexAllocation_ = VK_NULL_HANDLE;
    uint32_t indexCount_ = 0;
};

} // namespace iron3d
