#pragma once

// Implementacion Vulkan de IRenderer (Fase 3): envuelve VulkanContext,
// SwapchainWindow, LitPipeline para la malla de escena (Fase 5, con
// iluminacion) y un BasicPipeline por cada combinacion topologia/cull que
// sigue sin iluminar (lineas del gizmo, triangulos del gizmo) + GizmoMesh
// para su buffer dinamico.

#include "IRenderer.h"
#include "VulkanContext.h"
#include "SwapchainWindow.h"
#include "BasicPipeline.h"
#include "LitPipeline.h"
#include "ShadowMap.h"
#include "SsaoPass.h"
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
    void beginShadowPass(const glm::mat4& lightViewProj) override;
    void drawMeshShadow(MeshHandle mesh, const glm::mat4& model) override;
    void endShadowPass() override;
    void beginGBufferPass(const glm::mat4& view, const glm::mat4& proj) override;
    void drawMeshGBuffer(MeshHandle mesh, const glm::mat4& model) override;
    void endGBufferPass() override;
    void beginMainPass() override;
    void setViewportRect(int x, int y, int width, int height) override;
    void drawGrid(const std::vector<Vertex>& lineVerts, const glm::mat4& viewProj) override;
    void setLighting(const glm::vec3& lightPosWS, const glm::vec3& eyePosWS,
                      const glm::mat4& lightViewProj) override;
    void drawMesh(MeshHandle mesh, const glm::mat4& model, const glm::mat4& mvp,
                  const Material& material) override;
    void drawGizmo(const std::vector<Vertex>& lineVerts, const std::vector<Vertex>& triVerts,
                    const glm::mat4& viewProj) override;
    void endFrame() override;

    void imguiNewFrame() override;

private:
    VulkanContext ctx_;
    SwapchainWindow swapchain_;
    VmaAllocator allocator_ = VK_NULL_HANDLE;

    LitPipeline litPipeline_; // malla de escena con iluminacion (Fase 5)
    ShadowMap shadowMap_;     // mapa de sombras de la unica luz (Fase render 2)
    SsaoPass ssaoPass_;       // oclusion ambiental en espacio de pantalla (Fase 3)
    BasicPipeline gizmoLinePipeline_;
    BasicPipeline gizmoTriPipeline_;
    GizmoMesh gizmoMesh_;
    GizmoMesh gridMesh_;
    int vpX_ = 0, vpY_ = 0, vpW_ = 0, vpH_ = 0; // 0x0 = framebuffer entero

    std::vector<PrimitiveMesh> meshes_; // indexado por MeshHandle

    glm::vec3 lightPosWS_{0.0f}; // fijado por setLighting(), leido en cada drawMesh() de este frame
    glm::vec3 eyePosWS_{0.0f};
    glm::mat4 lightViewProj_{1.0f}; // idem -- tambien la usa drawMeshShadow() durante beginShadowPass()

    VkCommandBuffer currentCmd_ = VK_NULL_HANDLE; // valido solo entre beginFrame()/endFrame()
};

} // namespace iron3d
