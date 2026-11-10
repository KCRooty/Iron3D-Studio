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
#include "MaterialTextures.h"
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
    void setDrawRect(int x, int y, int width, int height) override;
    void drawGrid(const std::vector<Vertex>& lineVerts, const glm::mat4& viewProj) override;
    void setLighting(const glm::vec3& lightPosWS, const glm::vec3& eyePosWS,
                      const glm::mat4& lightViewProj) override;
    void drawMesh(MeshHandle mesh, const glm::mat4& model, const glm::mat4& mvp,
                  const Material& material, const TexMapping& tex) override;
    void drawGizmo(const std::vector<Vertex>& lineVerts, const std::vector<Vertex>& triVerts,
                    const glm::mat4& viewProj) override;
    void endFrame() override;

    void imguiNewFrame() override;

    void* createUiTexture(const uint8_t* rgba, int width, int height) override;
    void destroyUiTexture(void* texture) override;

private:
    // Texturas de la UI (miniaturas): imagen en GPU + descriptor de ImGui.
    struct UiTexture {
        VkImage image = VK_NULL_HANDLE;
        VmaAllocation alloc = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VkDescriptorSet set = VK_NULL_HANDLE;
    };
    void freeUiTexture(const UiTexture& t);
    std::vector<UiTexture> uiTextures_;
    struct PendingUiDestroy { UiTexture tex; int framesLeft; };
    std::vector<PendingUiDestroy> pendingUiDestroy_;
    VkSampler uiSampler_ = VK_NULL_HANDLE;
    VkCommandPool uploadPool_ = VK_NULL_HANDLE;

    VulkanContext ctx_;
    SwapchainWindow swapchain_;
    VmaAllocator allocator_ = VK_NULL_HANDLE;

    LitPipeline litPipeline_; // malla de escena con iluminacion (Fase 5)
    ShadowMap shadowMap_;     // mapa de sombras de la unica luz (Fase render 2)
    SsaoPass ssaoPass_;       // oclusion ambiental en espacio de pantalla (Fase 3)
    MaterialTextures materialTextures_; // texturas de Color de los materiales (2026-11-10)
    BasicPipeline gizmoLinePipeline_;
    BasicPipeline gizmoTriPipeline_;
    GizmoMesh gizmoMesh_;
    GizmoMesh gridMesh_;
    int vpX_ = 0, vpY_ = 0, vpW_ = 0, vpH_ = 0; // 0x0 = framebuffer entero

    std::vector<PrimitiveMesh> meshes_; // indexado por MeshHandle
    // Borrado diferido (2026-11-04): destroyMesh() no libera en el acto
    // porque el frame anterior puede seguir usando el buffer en la GPU. Se
    // apunta aqui y beginFrame() lo libera unos frames despues; el hueco
    // pasa a freeMeshes_ para reutilizarlo en el siguiente createMesh().
    struct PendingDestroy { MeshHandle handle; int framesLeft; };
    std::vector<PendingDestroy> pendingDestroy_;
    std::vector<MeshHandle> freeMeshes_;

    glm::vec3 lightPosWS_{0.0f}; // fijado por setLighting(), leido en cada drawMesh() de este frame
    glm::vec3 eyePosWS_{0.0f};
    glm::mat4 lightViewProj_{1.0f}; // idem -- tambien la usa drawMeshShadow() durante beginShadowPass()

    VkCommandBuffer currentCmd_ = VK_NULL_HANDLE; // valido solo entre beginFrame()/endFrame()
};

} // namespace iron3d
