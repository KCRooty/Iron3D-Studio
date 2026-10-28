#pragma once

// Interfaz de render independiente del backend (Fase 3, Documento Maestro:
// "Vulkan por defecto, OpenGL elegible en Preferencias o en la instalacion").
// Deliberadamente NO tiene forma de command-buffer de Vulkan: son las
// operaciones conceptuales que main.cpp necesita (crear malla, dibujar una
// malla con una MVP, dibujar el gizmo, empezar/acabar un frame), para que el
// backend de OpenGL -- que no tiene swapchain/render pass/semaforos que
// gestionar a mano -- no tenga que fingir que los tiene. Cada backend
// esconde su propia plomeria (pipelines de Vulkan, VAOs de OpenGL, shaders)
// detras de esto.

#include "core/MeshData.h"
#include "core/Vertex.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <memory>
#include <vector>

struct GLFWwindow;

namespace iron3d {

enum class RendererBackend { Vulkan, OpenGL };

using MeshHandle = uint32_t;
constexpr MeshHandle kInvalidMesh = UINT32_MAX;

// Hints de GLFW que hay que fijar ANTES de glfwCreateWindow (cliente API,
// version/perfil de contexto para OpenGL) -- distintos por backend, por eso
// no se puede crear la ventana antes de saber cual se va a usar.
void configureWindowHints(RendererBackend backend);

class IRenderer {
public:
    virtual ~IRenderer() = default;

    // Crea el contexto/dispositivo de este backend sobre `window` (que ya
    // debe existir, con los hints de configureWindowHints() ya aplicados) y
    // inicializa tambien los backends de Dear ImGui que le correspondan
    // (ImGui_ImplGlfw_InitFor* + ImGui_Impl<Backend>_Init). Requiere que
    // ImGui::CreateContext() ya se haya llamado.
    virtual void init(GLFWwindow* window, int width, int height) = 0;
    // Espera a que la GPU termine, destruye los backends de ImGui y todo lo
    // creado por init()/createMesh(). NO llama a ImGui::DestroyContext()
    // (eso es responsabilidad de main.cpp, es independiente del backend).
    virtual void shutdown() = 0;

    virtual void resizeIfNeeded(int fbWidth, int fbHeight) = 0;
    virtual void setClearColor(float r, float g, float b, float a) = 0;

    // Malla estatica subida una vez (cubo/esfera/plano -- ver PrimitiveMesh).
    virtual MeshHandle createMesh(const MeshData& data) = 0;
    virtual void destroyMesh(MeshHandle handle) = 0;

    // true si hay que dibujar este frame; false si hay que saltarselo
    // (minimizada, swapchain fuera de fecha en Vulkan...) -- en ese caso no
    // llamar a drawMesh/drawGizmo/endFrame.
    virtual bool beginFrame() = 0;
    virtual void drawMesh(MeshHandle mesh, const glm::mat4& mvp) = 0;
    // Vertices ya en espacio de mundo (ver TransformGizmo::buildGeometry):
    // el backend se encarga de subirlos a un buffer dinamico cada frame.
    virtual void drawGizmo(const std::vector<Vertex>& lineVerts, const std::vector<Vertex>& triVerts,
                            const glm::mat4& viewProj) = 0;
    // Compone ImGui::GetDrawData() encima de lo ya dibujado y presenta.
    // Llamar despues de ImGui::Render().
    virtual void endFrame() = 0;

    // ImGui_Impl<Backend>_NewFrame() -- la mitad de ImGui_ImplGlfw_NewFrame()
    // es comun a los dos backends y se llama aparte, en main.cpp.
    virtual void imguiNewFrame() = 0;
};

std::unique_ptr<IRenderer> createRenderer(RendererBackend backend);

} // namespace iron3d
