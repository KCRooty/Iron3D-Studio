#pragma once

// Implementacion OpenGL de IRenderer (Fase 3b): alternativa a Vulkan
// elegible en Preferencias (default Vulkan, ver core/Preferences.h). Mucho
// mas simple que VulkanRenderer a proposito -- GL no tiene swapchain/render
// pass/semaforos que gestionar a mano, GLFW ya da el framebuffer por
// defecto y glfwSwapBuffers presenta, asi que no hace falta fingir que
// existe un "command buffer": cada drawMesh/drawGizmo graba directamente.
//
// Dos shader programs: `program_` (sin iluminar, mismo GLSL logico que
// shaders/basic.vert/.frag) para el gizmo, y `litProgram_` (Fase 5: Phong
// basico) para la malla de escena -- GL no necesita una "pipeline" separada
// por topologia/cullMode como Vulkan, eso es solo el parametro `mode` de
// glDraw*, asi que la unica razon para dos programs es que iluminan
// distinto.

#include "IRenderer.h"
#include "gl/GLLoader.h"

#include <vector>

namespace iron3d {

class OpenGLRenderer : public IRenderer {
public:
    void init(GLFWwindow* window, int width, int height) override;
    void shutdown() override;

    void resizeIfNeeded(int fbWidth, int fbHeight) override;
    void setClearColor(float r, float g, float b, float a) override;
    bool needsProjectionYFlip() const override { return false; }

    MeshHandle createMesh(const MeshData& data) override;
    void destroyMesh(MeshHandle handle) override;

    bool beginFrame() override;
    void setLighting(const glm::vec3& lightPosWS, const glm::vec3& eyePosWS) override;
    void drawMesh(MeshHandle mesh, const glm::mat4& model, const glm::mat4& mvp,
                  const Material& material) override;
    void drawGizmo(const std::vector<Vertex>& lineVerts, const std::vector<Vertex>& triVerts,
                    const glm::mat4& viewProj) override;
    void endFrame() override;

    void imguiNewFrame() override;

private:
    struct MeshResource {
        gl::GLuint vao = 0, vbo = 0, ebo = 0;
        gl::GLsizei indexCount = 0;
    };

    void createGizmoBuffers();

    GLFWwindow* window_ = nullptr;
    gl::GLuint program_ = 0;       // gizmo, sin iluminar (mismo de siempre)
    gl::GLint uMvpLocation_ = -1;

    // Programa con iluminacion basica (Fase 5), solo para drawMesh.
    gl::GLuint litProgram_ = 0;
    gl::GLint uLitModelLoc_ = -1, uLitMvpLoc_ = -1;
    gl::GLint uLitLightPosLoc_ = -1, uLitEyePosLoc_ = -1;
    gl::GLint uLitDiffuseLoc_ = -1, uLitSpecularLoc_ = -1, uLitShininessLoc_ = -1;
    glm::vec3 lightPosWS_{0.0f};
    glm::vec3 eyePosWS_{0.0f};

    float clearColor_[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    int width_ = 0, height_ = 0;

    std::vector<MeshResource> meshes_; // indexado por MeshHandle

    static constexpr gl::GLsizei kGizmoMaxVertices = 4096; // igual tope que GizmoMesh (Vulkan)
    gl::GLuint gizmoLineVao_ = 0, gizmoLineVbo_ = 0;
    gl::GLuint gizmoTriVao_ = 0, gizmoTriVbo_ = 0;
};

} // namespace iron3d
