#pragma once

// Malla GPU generica (Fase 2c): generalizacion de lo que antes era CubeMesh
// (Fase 1, solo el cubo) para aceptar cualquier MeshData -- cubo, esfera,
// plano, lo que haga falta despues. Sigue siendo un buffer estatico subido
// una vez (a diferencia de GizmoMesh, que se reescribe cada frame).

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include "MeshData.h"

namespace iron3d {

class PrimitiveMesh {
public:
    void create(VkDevice device, VmaAllocator allocator, const MeshData& data);
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
