// Iron3D Studio -- Fase 1 (Documento Maestro §8.1 punto 1):
//   "Esqueleto CMake, fase 1: ventana GLFW/SDL2 con ImGui; contexto Vulkan,
//    VMA y mallas basicas con camara orbital estilo C4D."
//
// Flujo de init adaptado del ejemplo oficial de Dear ImGui
// (examples/example_glfw_vulkan/main.cpp), con el cubo de Iron3D grabado
// DENTRO del mismo render pass, antes de componer la UI de ImGui encima.

#define GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>

#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "gfx/VulkanContext.h"
#include "gfx/SwapchainWindow.h"
#include "gfx/BasicPipeline.h"
#include "core/MeshData.h"
#include "core/PrimitiveMesh.h"
#include "core/OrbitalCamera.h"
#include "core/SceneNode.h"
#include "core/SceneHistory.h"
#include "core/TransformGizmo.h"
#include "core/GizmoMesh.h"
#include "ui/MainUi.h"

#include <cstdio>
#include <cstdlib>
#include <vector>
#include <filesystem>

#if defined(_WIN32)
#include <windows.h>
#elif defined(__linux__)
#include <unistd.h>
#include <climits>
#endif

namespace {

// Carpeta del ejecutable -- ahi vive shaders/ (lo copia el target CompileShaders
// de CMake junto al .exe/binario). Portable Windows/Linux, exigido por la
// paridad total del proyecto.
std::filesystem::path exeDirectory() {
#if defined(_WIN32)
    wchar_t buffer[MAX_PATH]{};
    GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    return std::filesystem::path(buffer).parent_path();
#elif defined(__linux__)
    char buffer[PATH_MAX]{};
    ssize_t len = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
    if (len <= 0) return std::filesystem::current_path();
    return std::filesystem::path(std::string(buffer, (size_t)len)).parent_path();
#else
    return std::filesystem::current_path();
#endif
}

void glfwErrorCallback(int error, const char* description) {
    std::fprintf(stderr, "[Iron3D][glfw] Error %d: %s\n", error, description);
}

void checkVkResult(VkResult err) {
    if (err == 0) return;
    std::fprintf(stderr, "[Iron3D][vulkan] VkResult = %d\n", (int)err);
    if (err < 0) std::abort();
}

double g_scrollY = 0.0;
void glfwScrollCallback(GLFWwindow* /*window*/, double /*xoffset*/, double yoffset) {
    g_scrollY += yoffset;
}

// Recorre el grafo de escena (Fase 2) y dibuja la malla comun que le toque a
// cada nodo segun su `primitive`, con su propia matriz de mundo. Un nodo
// invisible se salta entero junto con sus hijos -- igual que ocultar un
// objeto padre en cualquier DCC.
void drawScene(VkCommandBuffer cmd, iron3d::BasicPipeline& pipeline,
                iron3d::PrimitiveMesh& cubeMesh, iron3d::PrimitiveMesh& sphereMesh,
                iron3d::PrimitiveMesh& planeMesh, iron3d::SceneNode& node,
                const glm::mat4& parentWorld, const glm::mat4& viewProj) {
    if (!node.visible) return;
    glm::mat4 world = parentWorld * node.localMatrix();

    using iron3d::PrimitiveType;
    switch (node.primitive) {
        case PrimitiveType::Cube:
            pipeline.pushMvp(cmd, viewProj * world);
            cubeMesh.draw(cmd);
            break;
        case PrimitiveType::Sphere:
            pipeline.pushMvp(cmd, viewProj * world);
            sphereMesh.draw(cmd);
            break;
        case PrimitiveType::Plane:
            pipeline.pushMvp(cmd, viewProj * world);
            planeMesh.draw(cmd);
            break;
        case PrimitiveType::Light: {
            // Icono de tamano fijo, ignora la escala del nodo a proposito --
            // una luz no es geometria escalable, es un marcador (no ilumina
            // nada todavia, ver comentario en SceneNode.h).
            const glm::mat4 iconWorld = parentWorld *
                glm::translate(glm::mat4(1.0f), node.position) *
                glm::scale(glm::mat4(1.0f), glm::vec3(0.12f));
            pipeline.pushMvp(cmd, viewProj * iconWorld);
            sphereMesh.draw(cmd);
            break;
        }
        case PrimitiveType::None:
        default:
            break;
    }

    for (const auto& child : node.children()) {
        drawScene(cmd, pipeline, cubeMesh, sphereMesh, planeMesh, *child, world, viewProj);
    }
}

} // namespace

int main() {
    using namespace iron3d;

    glfwSetErrorCallback(glfwErrorCallback);
    if (!glfwInit()) {
        std::fprintf(stderr, "[Iron3D] glfwInit fallo.\n");
        return 1;
    }
    if (!glfwVulkanSupported()) {
        std::fprintf(stderr, "[Iron3D] Esta maquina no expone Vulkan via GLFW.\n");
        return 1;
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow* window = glfwCreateWindow(1280, 720, "Iron3D Studio -- Fase 1", nullptr, nullptr);
    if (!window) {
        std::fprintf(stderr, "[Iron3D] glfwCreateWindow fallo.\n");
        return 1;
    }
    glfwSetScrollCallback(window, glfwScrollCallback);

    // --- Vulkan: instancia, dispositivo, cola -------------------------------
    uint32_t glfwExtCount = 0;
    const char** glfwExt = glfwGetRequiredInstanceExtensions(&glfwExtCount);
    std::vector<const char*> instanceExtensions(glfwExt, glfwExt + glfwExtCount);

    VulkanContext ctx;
    ctx.init(instanceExtensions);

    VkSurfaceKHR surface = VK_NULL_HANDLE;
    vkCheck(glfwCreateWindowSurface(ctx.instance, window, nullptr, &surface), "glfwCreateWindowSurface");

    int fbWidth = 0, fbHeight = 0;
    glfwGetFramebufferSize(window, &fbWidth, &fbHeight);

    SwapchainWindow swapchain;
    swapchain.init(ctx.instance, ctx.physicalDevice, ctx.device, ctx.graphicsQueueFamily,
                    surface, fbWidth, fbHeight, /*minImageCount=*/2);

    // --- VMA -----------------------------------------------------------------
    VmaAllocatorCreateInfo allocatorInfo{};
    allocatorInfo.physicalDevice = ctx.physicalDevice;
    allocatorInfo.device = ctx.device;
    allocatorInfo.instance = ctx.instance;
    allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_3;
    VmaAllocator allocator = VK_NULL_HANDLE;
    vkCheck(vmaCreateAllocator(&allocatorInfo, &allocator), "vmaCreateAllocator");

    // --- Geometria y pipeline --------------------------------------------------
    // cullMode=NONE (en vez del BACK_BIT por defecto): con varias primitivas
    // nuevas (esfera, plano) no vale la pena re-verificar a mano el orden de
    // vertices de cada una contra VK_CULL_MODE_BACK_BIT -- a esta escala
    // (un puñado de objetos pequenos) el coste de no cullear es cero.
    PrimitiveMesh cubeMesh, sphereMesh, planeMesh;
    cubeMesh.create(ctx.device, allocator, makeCubeMesh());
    sphereMesh.create(ctx.device, allocator, makeSphereMesh());
    planeMesh.create(ctx.device, allocator, makePlaneMesh());

    BasicPipeline pipeline;
    pipeline.create(ctx.device, swapchain.renderPass(), exeDirectory() / "shaders",
                     VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VK_CULL_MODE_NONE);

    // Gizmo de transformacion (Fase 2b): mismos shaders basic.vert/.frag que el
    // cubo, dos pipelines extra solo por el cambio de topologia/cull que Vulkan
    // exige fijar en el objeto de pipeline (no es estado dinamico aqui).
    BasicPipeline gizmoLinePipeline;
    gizmoLinePipeline.create(ctx.device, swapchain.renderPass(), exeDirectory() / "shaders",
                              VK_PRIMITIVE_TOPOLOGY_LINE_LIST);
    BasicPipeline gizmoTriPipeline;
    gizmoTriPipeline.create(ctx.device, swapchain.renderPass(), exeDirectory() / "shaders",
                             VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VK_CULL_MODE_NONE);

    GizmoMesh gizmoMesh;
    gizmoMesh.create(allocator);
    TransformGizmo gizmo;
    std::vector<Vertex> gizmoLineVerts, gizmoTriVerts;

    OrbitalCamera camera;
    swapchain.setClearColor(0.08f, 0.08f, 0.10f, 1.0f); // gris muy oscuro, no negro plano

    // --- Grafo de escena (Fase 2) --------------------------------------------
    SceneNode sceneRoot("Escena");
    SceneNode* cubeNode = sceneRoot.addChild("Cubo", PrimitiveType::Cube);
    SceneNode* selected = cubeNode;
    SceneHistory history; // deshacer/rehacer (Fase 2d), ver SceneHistory.h

    // --- Carpeta de escenas (Fase 2c: guardar/cargar .iron) -------------------
    const std::filesystem::path scenesDir = exeDirectory() / "scenes";
    std::filesystem::create_directories(scenesDir);

    // --- Dear ImGui --------------------------------------------------------
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();

    ImGui_ImplGlfw_InitForVulkan(window, true);

    ImGui_ImplVulkan_InitInfo initInfo{};
    initInfo.Instance = ctx.instance;
    initInfo.PhysicalDevice = ctx.physicalDevice;
    initInfo.Device = ctx.device;
    initInfo.QueueFamily = ctx.graphicsQueueFamily;
    initInfo.Queue = ctx.graphicsQueue;
    initInfo.DescriptorPool = ctx.imguiDescriptorPool;
    initInfo.RenderPass = swapchain.renderPass();
    initInfo.Subpass = 0;
    initInfo.MinImageCount = 2;
    initInfo.ImageCount = swapchain.imageCount();
    initInfo.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    initInfo.Allocator = nullptr;
    initInfo.CheckVkResultFn = checkVkResult;
    initInfo.MinAllocationSize = 1024 * 1024;
    ImGui_ImplVulkan_Init(&initInfo);

    // --- Bucle principal -----------------------------------------------------
    double lastMouseX = 0.0, lastMouseY = 0.0;
    glfwGetCursorPos(window, &lastMouseX, &lastMouseY);
    camera.resetMouseDelta(lastMouseX, lastMouseY);

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        glfwGetFramebufferSize(window, &fbWidth, &fbHeight);
        swapchain.resizeIfNeeded(ctx.instance, ctx.physicalDevice, ctx.device,
                                  ctx.graphicsQueueFamily, fbWidth, fbHeight);
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

        const float aspect = (float)swapchain.width() / (float)swapchain.height();
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

        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        drawMainUi(sceneRoot, selected, gizmo.mode, scenesDir, history, io.Framerate);
        ImGui::Render();
        ImDrawData* drawData = ImGui::GetDrawData();

        gizmo.buildGeometry(selected, eye, gizmoLineVerts, gizmoTriVerts);
        gizmoMesh.upload(gizmoLineVerts, gizmoTriVerts);

        VkCommandBuffer cmd = swapchain.beginFrame(ctx.device);
        if (cmd != VK_NULL_HANDLE) {
            pipeline.bind(cmd);
            drawScene(cmd, pipeline, cubeMesh, sphereMesh, planeMesh, sceneRoot, glm::mat4(1.0f), viewProj);

            // Gizmo encima de la escena (ningun buffer de profundidad todavia en
            // Fase 1-2, asi que "despues" ya implica "por encima").
            gizmoTriPipeline.bind(cmd);
            gizmoTriPipeline.pushMvp(cmd, viewProj); // vertices del gizmo ya en espacio de mundo
            gizmoMesh.drawTriangles(cmd);
            gizmoLinePipeline.bind(cmd);
            gizmoLinePipeline.pushMvp(cmd, viewProj);
            gizmoMesh.drawLines(cmd);

            swapchain.endFrame(ctx.device, ctx.graphicsQueue, drawData); // graba ImGui y cierra el render pass
        }
    }

    // --- Cierre ----------------------------------------------------------------
    vkCheck(vkDeviceWaitIdle(ctx.device), "vkDeviceWaitIdle");

    gizmoMesh.destroy(allocator);
    gizmoLinePipeline.destroy(ctx.device);
    gizmoTriPipeline.destroy(ctx.device);
    pipeline.destroy(ctx.device);
    cubeMesh.destroy(ctx.device, allocator);
    sphereMesh.destroy(ctx.device, allocator);
    planeMesh.destroy(ctx.device, allocator);
    vmaDestroyAllocator(allocator);

    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    swapchain.destroy(ctx.instance, ctx.device); // tambien destruye `surface` (es wd_->Surface)
    ctx.shutdown();

    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
