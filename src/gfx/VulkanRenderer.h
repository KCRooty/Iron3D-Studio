#pragma once

// Implementacion Vulkan de IRenderer (Fase 3): envuelve exactamente lo que
// main.cpp hacia a mano hasta la Fase 2d -- VulkanContext, SwapchainWindow,
// un BasicPipeline por cada combinacion topologia/cull que hace falta
// (malla comun, lineas del gizmo, triangulos del gizmo) y GizmoMesh para el
// buffer dinamico del gizmo. Sin cambio de comportamiento respecto a la
// Fase 2d, solo movido detras de la interfaz comun.

#include "IRenderer.h"
#include "VulkanContext.h"
#include "SwapchainWindow.h"
#include "BasicPipeline.h"
#include "core/PrimitiveMesh.h"
#include "core/GizmoMesh.h"

#include <vk_mem_alloc.h>

#include <vector>

namespace iron3d {

class VulkanRenderer : public IRenderer {
public:
    void init(GLFWwindow* window, int width, int height) override;
    void shutdown() override;

    void resizeIfNeeded(int fbWidth, int fbHeight) override;
    void setClearColor(float r, float g, float b, float a) override;
    bool needsProjectionYFlip() const override { return true; }

    MeshHandle createMesh(const MeshData& data) override;
    void destroyMesh(MeshHandle handle) override;

    bool beginFrame() override;
    void drawMesh(MeshHandle mesh, const glm::mat4& mvp) override;
    void drawGizmo(const std::vector<Vertex>& lineVerts, const std::vector<Vertex>& triVerts,
                    const glm::mat4& viewProj) override;
    void endFrame() override;

    void imguiNewFrame() override;

private:
    VulkanContext ctx_;
    SwapchainWindow swapchain_;
    VmaAllocator allocator_ = VK_NULL_HANDLE;

    BasicPipeline meshPipeline_;
    BasicPipeline gizmoLinePipeline_;
    BasicPipeline gizmoTriPipeline_;
    GizmoMesh gizmoMesh_;

    std::vector<PrimitiveMesh> meshes_; // indexado por MeshHandle

    VkCommandBuffer currentCmd_ = VK_NULL_HANDLE; // valido solo entre beginFrame()/endFrame()
};

} // namespace iron3d
