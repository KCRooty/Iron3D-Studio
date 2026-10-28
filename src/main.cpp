// Iron3D Studio -- Fase 1 (Documento Maestro §8.1 punto 1):
//   "Esqueleto CMake, fase 1: ventana GLFW/SDL2 con ImGui; contexto Vulkan,
//    VMA y mallas basicas con camara orbital estilo C4D."
//
// Fase 3: main.cpp ya no sabe nada de Vulkan directamente -- todo el render
// pasa por IRenderer (ver gfx/IRenderer.h), que VulkanRenderer implementa
// envolviendo exactamente lo que este archivo hacia a mano hasta la Fase 2d.
// Permite anadir un backend OpenGL (Fase 3b) sin tocar esta logica.

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <imgui.h>
#include <imgui_impl_glfw.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "gfx/IRenderer.h"
#include "core/MeshData.h"
#include "core/OrbitalCamera.h"
#include "core/SceneNode.h"
#include "core/SceneHistory.h"
#include "core/TransformGizmo.h"
#include "core/Paths.h"
#include "ui/MainUi.h"

#include <cstdio>
#include <memory>
#include <vector>
#include <filesystem>

namespace {

void glfwErrorCallback(int error, const char* description) {
    std::fprintf(stderr, "[Iron3D][glfw] Error %d: %s\n", error, description);
}

double g_scrollY = 0.0;
void glfwScrollCallback(GLFWwindow* /*window*/, double /*xoffset*/, double yoffset) {
    g_scrollY += yoffset;
}

// Recorre el grafo de escena (Fase 2) y dibuja la malla comun que le toque a
// cada nodo segun su `primitive`, con su propia matriz de mundo. Un nodo
// invisible se salta entero junto con sus hijos -- igual que ocultar un
// objeto padre en cualquier DCC.
void drawScene(iron3d::IRenderer& renderer, iron3d::MeshHandle cubeMesh, iron3d::MeshHandle sphereMesh,
               iron3d::MeshHandle planeMesh, iron3d::SceneNode& node,
               const glm::mat4& parentWorld, const glm::mat4& viewProj) {
    if (!node.visible) return;
    glm::mat4 world = parentWorld * node.localMatrix();

    using iron3d::PrimitiveType;
    switch (node.primitive) {
        case PrimitiveType::Cube:
            renderer.drawMesh(cubeMesh, viewProj * world);
            break;
        case PrimitiveType::Sphere:
            renderer.drawMesh(sphereMesh, viewProj * world);
            break;
        case PrimitiveType::Plane:
            renderer.drawMesh(planeMesh, viewProj * world);
            break;
        case PrimitiveType::Light: {
            // Icono de tamano fijo, ignora la escala del nodo a proposito --
            // una luz no es geometria escalable, es un marcador (no ilumina
            // nada todavia, ver comentario en SceneNode.h).
            const glm::mat4 iconWorld = parentWorld *
                glm::translate(glm::mat4(1.0f), node.position) *
                glm::scale(glm::mat4(1.0f), glm::vec3(0.12f));
            renderer.drawMesh(sphereMesh, viewProj * iconWorld);
            break;
        }
        case PrimitiveType::None:
        default:
            break;
    }

    for (const auto& child : node.children()) {
        drawScene(renderer, cubeMesh, sphereMesh, planeMesh, *child, world, viewProj);
    }
}

} // namespace

int main() {
    using namespace iron3d;

    // --- Preferencias (Fase 3): que backend usar -- ver core/Preferences.h.
    // Por ahora fijo a Vulkan hasta que la Fase 3b (OpenGL) este lista.
    RendererBackend backend = RendererBackend::Vulkan;

    glfwSetErrorCallback(glfwErrorCallback);
    if (!glfwInit()) {
        std::fprintf(stderr, "[Iron3D] glfwInit fallo.\n");
        return 1;
    }

    configureWindowHints(backend);
    GLFWwindow* window = glfwCreateWindow(1280, 720, "Iron3D Studio", nullptr, nullptr);
    if (!window) {
        std::fprintf(stderr, "[Iron3D] glfwCreateWindow fallo.\n");
        return 1;
    }
    glfwSetScrollCallback(window, glfwScrollCallback);

    // --- Dear ImGui: el contexto se crea ANTES del renderer (init() de cada
    // backend llama a ImGui_ImplGlfw_InitFor*/ImGui_Impl<Backend>_Init, que
    // necesitan el contexto ya existente). ------------------------------
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();

    int fbWidth = 0, fbHeight = 0;
    glfwGetFramebufferSize(window, &fbWidth, &fbHeight);

    std::unique_ptr<IRenderer> renderer = createRenderer(backend);
    renderer->init(window, fbWidth, fbHeight);
    renderer->setClearColor(0.08f, 0.08f, 0.10f, 1.0f); // gris muy oscuro, no negro plano

    // --- Geometria --------------------------------------------------------
    MeshHandle cubeMesh = renderer->createMesh(makeCubeMesh());
    MeshHandle sphereMesh = renderer->createMesh(makeSphereMesh());
    MeshHandle planeMesh = renderer->createMesh(makePlaneMesh());

    TransformGizmo gizmo;
    std::vector<Vertex> gizmoLineVerts, gizmoTriVerts;

    OrbitalCamera camera;

    // --- Grafo de escena (Fase 2) --------------------------------------------
    SceneNode sceneRoot("Escena");
    SceneNode* cubeNode = sceneRoot.addChild("Cubo", PrimitiveType::Cube);
    SceneNode* selected = cubeNode;
    SceneHistory history; // deshacer/rehacer (Fase 2d), ver SceneHistory.h

    // --- Carpeta de escenas (Fase 2c: guardar/cargar .iron) -------------------
    const std::filesystem::path scenesDir = exeDirectory() / "scenes";
    std::filesystem::create_directories(scenesDir);

    // --- Bucle principal -----------------------------------------------------
    double lastMouseX = 0.0, lastMouseY = 0.0;
    glfwGetCursorPos(window, &lastMouseX, &lastMouseY);
    camera.resetMouseDelta(lastMouseX, lastMouseY);

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        glfwGetFramebufferSize(window, &fbWidth, &fbHeight);
        renderer->resizeIfNeeded(fbWidth, fbHeight);
        if (fbWidth <= 0 || fbHeight <= 0) {
            continue; // minimizada: no hay nada que dibujar este frame
        }

        double mouseX = 0.0, mouseY = 0.0;
        glfwGetCursorPos(window, &mouseX, &mouseY);
        const bool altHeld = glfwGetKey(window, GLFW_KEY_LEFT_ALT) == GLFW_PRESS ||
                              glfwGetKey(window, GLFW_KEY_RIGHT_ALT) == GLFW_PRESS;
        if (!io.WantCaptureMouse) {
            camera.handleInput(window, mouseX, mouseY, g_scrollY);
        } else {
            camera.resetMouseDelta(mouseX, mouseY); // evita un salto al soltar un panel de ImGui
        }
        g_scrollY = 0.0;

        const float aspect = (float)fbWidth / (float)fbHeight;
        const glm::mat4 view = camera.viewMatrix();
        const glm::mat4 proj = camera.projectionMatrix(aspect);
        const glm::vec3 eye = camera.eyePosition();
        const glm::mat4 viewProj = proj * view;

        // Picking/arrastre del gizmo: solo si el raton no esta sobre un panel de
        // ImGui y no se esta orbitando con Alt (misma condicion que la camara,
        // para que nunca compitan por el mismo clic).
        const bool leftDown = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
        if (!io.WantCaptureMouse && !altHeld) {
            const Ray mouseRay = TransformGizmo::screenRay(mouseX, mouseY, fbWidth, fbHeight, view, proj, eye);
            if (leftDown) {
                if (gizmo.isDragging()) {
                    gizmo.updateDrag(mouseRay);
                } else {
                    // Snapshot ANTES de intentar el drag: si agarra un eje de
                    // verdad (beginDrag devuelve true) se confirma como paso
                    // de deshacer; si no agarra nada, se descarta sin mas.
                    const std::string preDragSnap = history.snapshot(sceneRoot);
                    if (gizmo.beginDrag(mouseRay, selected, eye)) {
                        history.commit(preDragSnap);
                    }
                }
            } else {
                gizmo.endDrag();
                gizmo.updateHover(mouseRay, selected, eye);
            }
        } else {
            gizmo.endDrag();
        }

        renderer->imguiNewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        drawMainUi(sceneRoot, selected, gizmo.mode, scenesDir, history, io.Framerate);
        ImGui::Render();

        gizmo.buildGeometry(selected, eye, gizmoLineVerts, gizmoTriVerts);

        if (renderer->beginFrame()) {
            drawScene(*renderer, cubeMesh, sphereMesh, planeMesh, sceneRoot, glm::mat4(1.0f), viewProj);

            // Gizmo encima de la escena (ningun buffer de profundidad todavia en
            // Fase 1-2, asi que "despues" ya implica "por encima").
            renderer->drawGizmo(gizmoLineVerts, gizmoTriVerts, viewProj);

            renderer->endFrame(); // compone ImGui y presenta
        }
    }

    // --- Cierre ----------------------------------------------------------------
    renderer->destroyMesh(cubeMesh);
    renderer->destroyMesh(sphereMesh);
    renderer->destroyMesh(planeMesh);
    renderer->shutdown();
    renderer.reset();

    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
