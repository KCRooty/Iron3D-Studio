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
#include "core/Material.h"

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

    // true para Vulkan (+Y hacia abajo en espacio de recorte), false para
    // OpenGL -- ver OrbitalCamera::projectionMatrix().
    virtual bool needsProjectionYFlip() const = 0;

    // Malla estatica subida una vez (cubo/esfera/plano -- ver PrimitiveMesh).
    virtual MeshHandle createMesh(const MeshData& data) = 0;
    virtual void destroyMesh(MeshHandle handle) = 0;

    // true si hay que dibujar este frame; false si hay que saltarselo
    // (minimizada, swapchain fuera de fecha en Vulkan...) -- en ese caso no
    // llamar a nada mas de esta lista hasta el siguiente beginFrame().
    // Ya NO deja el render pass principal abierto (Fase render 2): entre
    // esto y beginMainPass() es donde va la pasada de sombras (ver abajo),
    // que en Vulkan necesita el command buffer abierto pero sin ningun
    // render pass en marcha todavia.
    virtual bool beginFrame() = 0;

    // Pasada de profundidad desde el punto de vista de la luz (Fase render
    // 2, shadow mapping): beginShadowPass/drawMeshShadow/endShadowPass se
    // llaman ANTES de beginMainPass(), una vez por nodo con geometria de la
    // escena (mismo recorrido que drawScene() en main.cpp, sin material --
    // solo hace falta profundidad). En OpenGL (sin sombras todavia, ver
    // ROADMAP_RENDER.md) estos tres son no-ops.
    virtual void beginShadowPass(const glm::mat4& lightViewProj) = 0;
    virtual void drawMeshShadow(MeshHandle mesh, const glm::mat4& model) = 0;
    virtual void endShadowPass() = 0;

    // Pasada de G-buffer para oclusion ambiental en espacio de pantalla
    // (Fase 3, SSAO -- ver ROADMAP_RENDER.md): beginGBufferPass/
    // drawMeshGBuffer/endGBufferPass se llaman DESPUES de endShadowPass() y
    // ANTES de beginMainPass() -- mismo recorrido de escena que la pasada de
    // sombras (sin material, solo posicion/normal en espacio de vista).
    // endGBufferPass() ya deja encadenadas las pasadas de SSAO + blur (ver
    // SsaoPass); el resultado esta listo para que drawMesh() lo muestree.
    // En OpenGL (sin SSAO, mismo criterio que las sombras) estos tres son
    // no-ops.
    virtual void beginGBufferPass(const glm::mat4& view, const glm::mat4& proj) = 0;
    virtual void drawMeshGBuffer(MeshHandle mesh, const glm::mat4& model) = 0;
    virtual void endGBufferPass() = 0;

    // Abre el render pass/estado principal de color (en Vulkan; en OpenGL no
    // hace falta, ya lo dejo listo beginFrame()). Llamar despues de
    // endGBufferPass(), antes de setViewportRect/drawGrid/drawMesh/drawGizmo.
    virtual void beginMainPass() = 0;

    // Rectangulo del visor 3D dentro del framebuffer (pixeles, origen
    // arriba-izquierda). La escena, la cuadricula y el gizmo solo se dibujan
    // ahi; el resto de la ventana son paneles de ImGui, igual que en C4D.
    // Se aplica en el siguiente beginFrame().
    virtual void setViewportRect(int x, int y, int width, int height) = 0;

    // Cuadricula de suelo: lineas en espacio de mundo, sin test de
    // profundidad y sin escribirla -- llamar ANTES de drawMesh para que la
    // geometria la tape. Buffer propio, independiente del gizmo.
    virtual void drawGrid(const std::vector<Vertex>& lineVerts, const glm::mat4& viewProj) = 0;

    // Estado de iluminacion de ESTE frame: una sola luz puntual + un termino
    // ambiente tipo IBL aproximado (ver lit.frag) -- la "Luz" de la escena
    // (o el foco de camara de respaldo si no hay ninguna, ver main.cpp).
    // `lightViewProj` es la misma matriz que beginShadowPass() uso para
    // rellenar el mapa de sombras (Fase render 2); en OpenGL se ignora
    // (sin sombras todavia). Llamar una vez por frame, antes de cualquier
    // drawMesh.
    virtual void setLighting(const glm::vec3& lightPosWS, const glm::vec3& eyePosWS,
                              const glm::mat4& lightViewProj) = 0;
    // `model` es la matriz de mundo del nodo (para posicion/normal en espacio
    // de mundo); `mvp` ya trae la proyeccion*vista*mundo para gl_Position --
    // se pasan ambas en vez de reconstruir una desde la otra en el shader.
    virtual void drawMesh(MeshHandle mesh, const glm::mat4& model, const glm::mat4& mvp,
                           const Material& material) = 0;
    // Vertices ya en espacio de mundo (ver TransformGizmo::buildGeometry):
    // el backend se encarga de subirlos a un buffer dinamico cada frame.
    virtual void drawGizmo(const std::vector<Vertex>& lineVerts, const std::vector<Vertex>& triVerts,
                            const glm::mat4& viewProj) = 0;
    // Compone ImGui::GetDrawData() encima de lo ya dibujado y presenta.
    // Llamar despues de ImGui::Render().
    virtual void endFrame() = 0;

    // Textura RGBA8 para dibujar con ImGui (miniaturas del Navegador de
    // Contenido). Devuelve el ImTextureID del backend (VkDescriptorSet en
    // Vulkan, nombre de textura en OpenGL) o nullptr si no se pudo crear.
    // destroyUiTexture() la libera cuando la GPU ya no la usa.
    virtual void* createUiTexture(const uint8_t* rgba, int width, int height) = 0;
    virtual void destroyUiTexture(void* texture) = 0;

    // ImGui_Impl<Backend>_NewFrame() -- la mitad de ImGui_ImplGlfw_NewFrame()
    // es comun a los dos backends y se llama aparte, en main.cpp.
    virtual void imguiNewFrame() = 0;
};

std::unique_ptr<IRenderer> createRenderer(RendererBackend backend);

} // namespace iron3d
