#pragma once

// Implementacion OpenGL de IRenderer (Fase 3b): alternativa a Vulkan
// elegible en Preferencias (default Vulkan, ver core/Preferences.h). Mucho
// mas simple que VulkanRenderer a proposito -- GL no tiene swapchain/render
// pass/semaforos que gestionar a mano, GLFW ya da el framebuffer por
// defecto y glfwSwapBuffers presenta, asi que no hace falta fingir que
// existe un "command buffer": cada drawMesh/drawGizmo graba directamente.
//
// Un solo shader program (mismo GLSL logico que shaders/basic.vert/.frag,
// solo que con un uniform en vez de un push constant de Vulkan) para malla
// y gizmo: GL no necesita una "pipeline" separada por topologia/cullMode
// como Vulkan, eso es solo el parametro `mode` de glDraw*.

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
    void drawMesh(MeshHandle mesh, const glm::mat4& mvp) override;
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
    gl::GLuint program_ = 0;
    gl::GLint uMvpLocation_ = -1;
    float clearColor_[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    int width_ = 0, height_ = 0;

    std::vector<MeshResource> meshes_; // indexado por MeshHandle

    static constexpr gl::GLsizei kGizmoMaxVertices = 4096; // igual tope que GizmoMesh (Vulkan)
    gl::GLuint gizmoLineVao_ = 0, gizmoLineVbo_ = 0;
    gl::GLuint gizmoTriVao_ = 0, gizmoTriVbo_ = 0;
};

} // namespace iron3d
