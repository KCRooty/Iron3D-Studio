#pragma once

// Buffers de vertices dinamicos para el gizmo de transformacion (Fase 2b):
// a diferencia del cubo (geometria fija, Fase 1), la forma del gizmo cambia
// cada frame (eje resaltado al pasar el raton, tamano segun distancia de
// camara), asi que se sube de nuevo cada frame a un buffer host-visible
// mapeado de forma persistente -- mismo patron que CubeMesh pero escribible.

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include "Vertex.h"

#include <vector>

namespace iron3d {

class GizmoMesh {
public:
    void create(VmaAllocator allocator, uint32_t maxVertices = 4096);
    void destroy(VmaAllocator allocator);

    // Sobrescribe el contenido de ambos buffers para este frame. Si un vector
    // supera `maxVertices` se trunca (no deberia pasar nunca con 3 ejes).
    void upload(const std::vector<Vertex>& lineVerts, const std::vector<Vertex>& triVerts);

    void drawLines(VkCommandBuffer cmd) const;
    void drawTriangles(VkCommandBuffer cmd) const;

private:
    struct DynamicBuffer {
        VkBuffer buffer = VK_NULL_HANDLE;
        VmaAllocation allocation = VK_NULL_HANDLE;
        void* mapped = nullptr;
        uint32_t capacity = 0;
        uint32_t count = 0;
    };

    static void createBuffer(VmaAllocator allocator, uint32_t maxVertices, DynamicBuffer& out);
    static void destroyBuffer(VmaAllocator allocator, DynamicBuffer& buf);
    static void writeBuffer(DynamicBuffer& buf, const std::vector<Vertex>& verts);

    DynamicBuffer lines_;
    DynamicBuffer triangles_;
};

} // namespace iron3d
