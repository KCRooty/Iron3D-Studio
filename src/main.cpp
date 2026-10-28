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
#include "core/Preferences.h"
#include "core/Material.h"
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

// Recorre el grafo de escena (Fase 2) buscando la primera Luz (Fase 5): su
// posicion en mundo es la que usa drawScene() para iluminar todo lo demas.
// Solo una luz activa por ahora -- Phong basico, no hace falta mas.
bool findFirstLightWorldPos(iron3d::SceneNode& node, const glm::mat4& parentWorld, glm::vec3& outPos) {
    if (!node.visible) return false;
    const glm::mat4 world = parentWorld * node.localMatrix();
    if (node.primitive == iron3d::PrimitiveType::Light) {
        outPos = glm::vec3(world * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
        return true;
    }
    for (const auto& child : node.children()) {
        if (findFirstLightWorldPos(*child, world, outPos)) return true;
    }
    return false;
}

// Recorre el grafo de escena (Fase 2) y dibuja la malla comun que le toque a
// cada nodo segun su `primitive`, con su propia matriz de mundo e
// iluminacion (Fase 5: material del nodo, o el gris por defecto si no tiene
// uno asignado). Un nodo invisible se salta entero junto con sus hijos --
// igual que ocultar un objeto padre en cualquier DCC.
void drawScene(iron3d::IRenderer& renderer, iron3d::MeshHandle cubeMesh, iron3d::MeshHandle sphereMesh,
               iron3d::MeshHandle planeMesh, const std::vector<iron3d::Material>& materials,
               iron3d::SceneNode& node, const glm::mat4& parentWorld, const glm::mat4& viewProj) {
    if (!node.visible) return;
    glm::mat4 world = parentWorld * node.localMatrix();
    const iron3d::Material& material = iron3d::materialOrDefault(materials, node.materialIndex);

    using iron3d::PrimitiveType;
    switch (node.primitive) {
        case PrimitiveType::Cube:
            renderer.drawMesh(cubeMesh, world, viewProj * world, material);
            break;
        case PrimitiveType::Sphere:
            renderer.drawMesh(sphereMesh, world, viewProj * world, material);
            break;
        case PrimitiveType::Plane:
            renderer.drawMesh(planeMesh, world, viewProj * world, material);
            break;
        case PrimitiveType::Light: {
            // Icono de tamano fijo, ignora la escala del nodo a proposito --
            // una luz no es geometria escalable, es un marcador. Ahora SI se
            // ilumina de verdad (es geometria normal a ojos del renderer),
            // lo cual de paso hace que se vea donde esta la luz.
            const glm::mat4 iconWorld = parentWorld *
                glm::translate(glm::mat4(1.0f), node.position) *
                glm::scale(glm::mat4(1.0f), glm::vec3(0.12f));
            renderer.drawMesh(sphereMesh, iconWorld, viewProj * iconWorld, material);
            break;
        }
        case PrimitiveType::None:
        default:
            break;
    }

    for (const auto& child : node.children()) {
        drawScene(renderer, cubeMesh, sphereMesh, planeMesh, materials, *child, world, viewProj);
    }
}

} // namespace

int main() {
    using namespace iron3d;

    // --- Preferencias (Fase 3c): que backend usar -- ver core/Preferences.h.
    // Se lee ANTES de crear la ventana (el backend decide los hints de
    // GLFW); el archivo vive junto al ejecutable, igual que la carpeta
    // scenes/. Si no existe o no parsea, loadPreferences devuelve el default
    // (Vulkan) sin tocar nada.
    const std::filesystem::path prefsPath = exeDirectory() / "preferences.cfg";
    Preferences prefs = loadPreferences(prefsPath);
    RendererBackend backend = prefs.renderBackend;

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
    std::vector<Material> materials; // biblioteca de materiales (Fase 5), ver core/Material.h
    SceneHistory history(materials); // deshacer/rehacer (Fase 2d), ver SceneHistory.h

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
        const glm::mat4 proj = camera.projectionMatrix(aspect, renderer->needsProjectionYFlip());
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
        drawMainUi(sceneRoot, selected, gizmo.mode, scenesDir, history, io.Framerate, backend, prefs, prefsPath,
                   materials);
        ImGui::Render();

        gizmo.buildGeometry(selected, eye, gizmoLineVerts, gizmoTriVerts);

        if (renderer->beginFrame()) {
            // Luz de la escena (Fase 5): la primera que haya en el grafo; si no
            // hay ninguna, foco de camara de respaldo (headlamp) para que una
            // escena nueva sin Luz no se quede a oscuras.
            glm::vec3 lightPos = eye;
            findFirstLightWorldPos(sceneRoot, glm::mat4(1.0f), lightPos);
            renderer->setLighting(lightPos, eye);

            drawScene(*renderer, cubeMesh, sphereMesh, planeMesh, materials, sceneRoot, glm::mat4(1.0f), viewProj);

            // Gizmo encima de la escena (ningun buffer de profundidad todavia en
            // Fase 1-2, asi que "despues" ya implica "por encima").
            renderer->drawGizmo(gizmoLineVerts, gizmoTriVerts, viewProj);

            renderer->endFrame(); // compone ImGui y presenta
        }
    }

    // --- Cierre ----------------------------------------------------------------
    // No hace falta destroyMesh() aqui: renderer->shutdown() ya destruye todas
    // las mallas que tiene registradas (y lo hace tras su propio
    // vkDeviceWaitIdle en el backend Vulkan). Llamarlo antes, como se hacia,
    // destruia los VkBuffer mientras el ultimo frame enviado a la GPU
    // todavia los estaba usando -- validation error + crash en vez de cierre
    // limpio.
    renderer->shutdown();
    renderer.reset();

    // ImGui_ImplGlfw_Shutdown() NO va aqui: los dos backends (Vulkan y
    // OpenGL) ya lo llaman dentro de su propio shutdown() -- llamarlo otra
    // vez aqui era el "ya esta apagado" que hacia saltar el assert de ImGui
    // al cerrar.
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
