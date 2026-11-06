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
#include "core/SceneSerializer.h"
#include "ui/MainUi.h"
#include "ui/ContentBrowser.h"
#include "importers/lib4d/PresetLibrary.h"

#include "importers/cineware/CinewareImporter.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <unordered_map>
#include <vector>
#include <filesystem>
#include <functional>

namespace {

// Mallas en GPU (2026-11-04): las tres primitivas fijas + una entrada por
// cada malla propia (Objeto poligonal) que haya en la escena. La malla de
// CPU es compartida (SceneNode::mesh); cuando ningun nodo ni ningun paso de
// deshacer la usa, el weak_ptr caduca y collect() libera su copia en GPU.
struct SceneMeshes {
    iron3d::IRenderer* renderer = nullptr;
    iron3d::MeshHandle cube = iron3d::kInvalidMesh, sphere = iron3d::kInvalidMesh, plane = iron3d::kInvalidMesh;
    struct Entry { std::weak_ptr<const iron3d::MeshData> owner; iron3d::MeshHandle handle; };
    std::unordered_map<const iron3d::MeshData*, Entry> custom;

    iron3d::MeshHandle forNode(const iron3d::SceneNode& node) {
        using iron3d::PrimitiveType;
        switch (node.primitive) {
            case PrimitiveType::Cube: return cube;
            case PrimitiveType::Sphere: return sphere;
            case PrimitiveType::Plane: return plane;
            case PrimitiveType::Mesh: {
                if (!node.mesh || node.mesh->indices.empty()) return iron3d::kInvalidMesh;
                auto it = custom.find(node.mesh.get());
                if (it != custom.end() && !it->second.owner.expired()) return it->second.handle;
                if (it != custom.end()) { // misma direccion reutilizada por una malla nueva
                    renderer->destroyMesh(it->second.handle);
                    custom.erase(it);
                }
                const iron3d::MeshHandle h = renderer->createMesh(*node.mesh);
                custom.emplace(node.mesh.get(), Entry{node.mesh, h});
                return h;
            }
            default: return iron3d::kInvalidMesh;
        }
    }

    void collect() {
        for (auto it = custom.begin(); it != custom.end();) {
            if (it->second.owner.expired()) {
                renderer->destroyMesh(it->second.handle);
                it = custom.erase(it);
            } else {
                ++it;
            }
        }
    }
};

void glfwErrorCallback(int error, const char* description) {
    std::fprintf(stderr, "[Iron3D][glfw] Error %d: %s\n", error, description);
}

double g_scrollY = 0.0;
void glfwScrollCallback(GLFWwindow* /*window*/, double /*xoffset*/, double yoffset) {
    g_scrollY += yoffset;
}

// Archivos soltados desde el Explorador (rutas UTF-8 de GLFW). Se procesan en
// el bucle principal, despues de glfwPollEvents.
std::vector<std::filesystem::path> g_droppedFiles;
void glfwDropCallback(GLFWwindow* /*window*/, int count, const char** paths) {
    for (int i = 0; i < count; ++i) {
        const std::string p = paths[i];
        g_droppedFiles.emplace_back(std::u8string(p.begin(), p.end()));
    }
}

// Recorre el grafo de escena (Fase 2) buscando la primera Luz (Fase 5): su
// posicion en mundo es la que usa drawScene() para iluminar todo lo demas.
// Solo una luz activa por ahora -- Phong basico, no hace falta mas.
bool findFirstLightWorldPos(iron3d::SceneNode& node, const glm::mat4& parentWorld, glm::vec3& outPos,
                            bool parentVisible = true) {
    // Visibilidad de editor heredada (Por Defecto = la del padre); un hijo
    // en "Activado" se ve aunque su padre este apagado, como en C4D.
    const bool vis = iron3d::resolveVisibility(node.editorVis, parentVisible);
    const glm::mat4 world = parentWorld * node.localMatrix();
    if (vis && node.primitive == iron3d::PrimitiveType::Light) {
        outPos = glm::vec3(world * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
        return true;
    }
    for (const auto& child : node.children()) {
        if (findFirstLightWorldPos(*child, world, outPos, vis)) return true;
    }
    return false;
}

// Matriz vista*proyeccion de la luz para el mapa de sombras (Fase render 2,
// ver ROADMAP_RENDER.md): un unico shadow map ortografico, tratando la luz
// como si "mirara" al origen de la escena -- NO omnidireccional (un point
// light real necesitaria 6 pasadas a un cubemap, fuera de alcance aqui).
// Extension fija de la ortografica en vez de ajustarse al bounding box real
// de la escena: simplificacion deliberada de esta fase, de sobra para
// primitivas de ~200cm cerca del origen (el tamano por defecto de
// cualquiera nueva); un objeto muy lejos del origen quedara fuera del mapa
// y no proyectara sombra.
//
// Las constantes de abajo estan en unidades de MUNDO (las mismas que
// SceneNode::position/scale), no en "cm" -- kCmPerUnit=200 (MainUi.cpp)
// convierte cm<->unidad solo en los campos de la UI, nunca aqui. Bug real de
// esta fase (encontrado al verificar visualmente): con los valores viejos en
// cm literal (extent=600, far=5000) el frustum ortografico salia ~200x mas
// grande que la escena real (un cubo "de 200cm" son solo 1.0 unidades), asi
// que cualquier objeto ocupaba un par de pixeles del mapa de 2048 y la
// sombra resultante era invisible. 600cm/5000cm entre kCmPerUnit=200 da los
// valores de abajo.
glm::mat4 computeLightViewProj(const glm::vec3& lightPos) {
    const glm::vec3 target(0.0f);
    glm::vec3 forward = target - lightPos;
    float len = glm::length(forward);
    if (len < 1e-4f) { forward = glm::vec3(0.0f, -1.0f, 0.0f); len = 1.0f; } // luz en el origen: mirar hacia abajo
    forward /= len;
    glm::vec3 up(0.0f, 1.0f, 0.0f);
    if (std::abs(glm::dot(forward, up)) > 0.999f) {
        // La luz esta (casi) en el eje vertical que pasa por el origen:
        // lookAt con este up degenera (producto vectorial ~nulo).
        up = glm::vec3(0.0f, 0.0f, 1.0f);
    }
    const glm::mat4 lightView = glm::lookAt(lightPos, target, up);
    const float extent = 3.0f;   // = 600cm / kCmPerUnit
    const float nearZ = 0.01f;   // margen minimo delante de la luz
    const float farZ = 25.0f;    // = 5000cm / kCmPerUnit
    const glm::mat4 lightProj = glm::ortho(-extent, extent, -extent, extent, nearZ, farZ);
    return lightProj * lightView;
}

// Recorrido de solo-profundidad para el mapa de sombras (Fase render 2):
// mismo grafo que drawScene() pero sin materiales (no hace falta mas que la
// geometria) y sin el icono de Luz (un marcador decorativo proyectando su
// propia sombra justo en el origen de la luz no aporta nada).
void drawSceneShadow(iron3d::IRenderer& renderer, SceneMeshes& meshes, iron3d::SceneNode& node,
                      const glm::mat4& parentWorld, bool parentVisible = true) {
    const bool vis = iron3d::resolveVisibility(node.editorVis, parentVisible);
    const glm::mat4 world = parentWorld * node.localMatrix();

    if (vis && node.primitive != iron3d::PrimitiveType::Light) {
        const iron3d::MeshHandle h = meshes.forNode(node);
        if (h != iron3d::kInvalidMesh) renderer.drawMeshShadow(h, world);
    }

    for (const auto& child : node.children()) {
        drawSceneShadow(renderer, meshes, *child, world, vis);
    }
}

// Mismo recorrido que drawSceneShadow(), para el G-buffer de SSAO (Fase 3):
// tampoco hace falta material, solo la geometria para posicion/normal en
// espacio de vista.
void drawSceneGBuffer(iron3d::IRenderer& renderer, SceneMeshes& meshes, iron3d::SceneNode& node,
                       const glm::mat4& parentWorld, bool parentVisible = true) {
    const bool vis = iron3d::resolveVisibility(node.editorVis, parentVisible);
    const glm::mat4 world = parentWorld * node.localMatrix();

    if (vis && node.primitive != iron3d::PrimitiveType::Light) {
        const iron3d::MeshHandle h = meshes.forNode(node);
        if (h != iron3d::kInvalidMesh) renderer.drawMeshGBuffer(h, world);
    }

    for (const auto& child : node.children()) {
        drawSceneGBuffer(renderer, meshes, *child, world, vis);
    }
}

// Recorre el grafo de escena (Fase 2) y dibuja la malla comun que le toque a
// cada nodo segun su `primitive`, con su propia matriz de mundo e
// iluminacion (Fase 5: material del nodo, o el gris por defecto si no tiene
// uno asignado). La visibilidad del visor es el punto de ARRIBA del Gestor de
// Objetos (editorVis): Apagado oculta el nodo y a los hijos en Por Defecto,
// pero un hijo en Activado sigue viendose, igual que en C4D.
void drawScene(iron3d::IRenderer& renderer, SceneMeshes& meshes, const std::vector<iron3d::Material>& materials,
               iron3d::SceneNode& node, const glm::mat4& parentWorld, const glm::mat4& viewProj,
               bool parentVisible = true) {
    const bool vis = iron3d::resolveVisibility(node.editorVis, parentVisible);
    glm::mat4 world = parentWorld * node.localMatrix();
    const iron3d::Material& material = iron3d::materialOrDefault(materials, node.materialIndex);

    using iron3d::PrimitiveType;
    switch (vis ? node.primitive : PrimitiveType::None) {
        case PrimitiveType::Cube:
        case PrimitiveType::Sphere:
        case PrimitiveType::Plane:
        case PrimitiveType::Mesh: {
            const iron3d::MeshHandle h = meshes.forNode(node);
            if (h != iron3d::kInvalidMesh) renderer.drawMesh(h, world, viewProj * world, material);
            break;
        }
        case PrimitiveType::Light: {
            // Icono de tamano fijo, ignora la escala del nodo a proposito --
            // una luz no es geometria escalable, es un marcador. Ahora SI se
            // ilumina de verdad (es geometria normal a ojos del renderer),
            // lo cual de paso hace que se vea donde esta la luz.
            const glm::mat4 iconWorld = parentWorld *
                glm::translate(glm::mat4(1.0f), node.position) *
                glm::scale(glm::mat4(1.0f), glm::vec3(0.12f));
            renderer.drawMesh(meshes.sphere, iconWorld, viewProj * iconWorld, material);
            break;
        }
        case PrimitiveType::None:
        default:
            break;
    }

    for (const auto& child : node.children()) {
        drawScene(renderer, meshes, materials, *child, world, viewProj, vis);
    }
}

// Interseccion rayo-nodo para seleccionar con clic en el visor: el rayo se
// lleva al espacio local del nodo (inversa de su matriz de mundo), donde las
// primitivas son de tamano unidad, y la distancia se mide de vuelta en mundo.
// Rayo contra caja alineada [mn, mx] (metodo de las placas).
bool rayHitsBox(const glm::vec3& o, const glm::vec3& d, const glm::vec3& mn, const glm::vec3& mx, float& tOut) {
    float t0 = -1e30f, t1 = 1e30f;
    for (int i = 0; i < 3; ++i) {
        if (std::fabs(d[i]) < 1e-12f) {
            if (o[i] < mn[i] || o[i] > mx[i]) return false;
            continue;
        }
        float a = (mn[i] - o[i]) / d[i], b = (mx[i] - o[i]) / d[i];
        if (a > b) std::swap(a, b);
        t0 = std::max(t0, a);
        t1 = std::min(t1, b);
        if (t0 > t1) return false;
    }
    if (t1 < 0.0f) return false;
    tOut = t0 >= 0.0f ? t0 : t1;
    return true;
}

// Malla propia: primero la caja (descarte barato), luego cada triangulo
// (Moller-Trumbore, a dos caras como el render).
bool rayHitsMesh(const iron3d::MeshData& mesh, const glm::vec3& o, const glm::vec3& d, float& tOut) {
    float tb = 0.0f;
    const glm::vec3 pad(1e-4f);
    if (!rayHitsBox(o, d, mesh.boundsMin - pad, mesh.boundsMax + pad, tb)) return false;
    bool hit = false;
    float best = 1e30f;
    for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        const glm::vec3& a = mesh.vertices[mesh.indices[i]].position;
        const glm::vec3& b = mesh.vertices[mesh.indices[i + 1]].position;
        const glm::vec3& c = mesh.vertices[mesh.indices[i + 2]].position;
        const glm::vec3 e1 = b - a, e2 = c - a, pv = glm::cross(d, e2);
        const float det = glm::dot(e1, pv);
        if (std::fabs(det) < 1e-12f) continue;
        const float inv = 1.0f / det;
        const glm::vec3 tv = o - a;
        const float u = glm::dot(tv, pv) * inv;
        if (u < 0.0f || u > 1.0f) continue;
        const glm::vec3 qv = glm::cross(tv, e1);
        const float v = glm::dot(d, qv) * inv;
        if (v < 0.0f || u + v > 1.0f) continue;
        const float t = glm::dot(e2, qv) * inv;
        if (t >= 0.0f && t < best) { best = t; hit = true; }
    }
    if (hit) tOut = best;
    return hit;
}

bool rayHitsLocal(const iron3d::SceneNode& node, const glm::vec3& o, const glm::vec3& d, float& tOut) {
    using iron3d::PrimitiveType;
    switch (node.primitive) {
        case PrimitiveType::Cube: {
            float t0 = -1e30f, t1 = 1e30f;
            for (int i = 0; i < 3; ++i) {
                if (std::fabs(d[i]) < 1e-8f) {
                    if (o[i] < -0.5f || o[i] > 0.5f) return false;
                    continue;
                }
                float a = (-0.5f - o[i]) / d[i], b = (0.5f - o[i]) / d[i];
                if (a > b) std::swap(a, b);
                t0 = std::max(t0, a);
                t1 = std::min(t1, b);
                if (t0 > t1) return false;
            }
            if (t1 < 0.0f) return false;
            tOut = t0 >= 0.0f ? t0 : t1;
            return true;
        }
        case PrimitiveType::Sphere:
        case PrimitiveType::Light: {
            const float b = glm::dot(o, d), c = glm::dot(o, o) - 0.25f, a = glm::dot(d, d);
            const float disc = b * b - a * c;
            if (disc < 0.0f) return false;
            float t = (-b - std::sqrt(disc)) / a;
            if (t < 0.0f) t = (-b + std::sqrt(disc)) / a;
            if (t < 0.0f) return false;
            tOut = t;
            return true;
        }
        case PrimitiveType::Plane: {
            if (std::fabs(d.y) < 1e-8f) return false;
            const float t = -o.y / d.y;
            if (t < 0.0f) return false;
            const glm::vec3 p = o + d * t;
            if (std::fabs(p.x) > 0.5f || std::fabs(p.z) > 0.5f) return false;
            tOut = t;
            return true;
        }
        case PrimitiveType::Mesh:
            return node.mesh && rayHitsMesh(*node.mesh, o, d, tOut);
        default:
            return false;
    }
}

void pickNode(iron3d::SceneNode& node, const glm::mat4& parentWorld, const iron3d::Ray& ray,
              iron3d::SceneNode*& best, float& bestDist, bool parentVisible = true) {
    const bool vis = iron3d::resolveVisibility(node.editorVis, parentVisible);
    const glm::mat4 world = parentWorld * node.localMatrix();
    if (vis && node.primitive != iron3d::PrimitiveType::None) {
        glm::mat4 m = world;
        if (node.primitive == iron3d::PrimitiveType::Light) {
            // Mismo icono de tamano fijo que dibuja drawScene().
            m = parentWorld * glm::translate(glm::mat4(1.0f), node.position) *
                glm::scale(glm::mat4(1.0f), glm::vec3(0.12f));
        }
        const glm::mat4 inv = glm::inverse(m);
        const glm::vec3 lo = glm::vec3(inv * glm::vec4(ray.origin, 1.0f));
        const glm::vec3 ld = glm::vec3(inv * glm::vec4(ray.direction, 0.0f));
        float t = 0.0f;
        if (rayHitsLocal(node, lo, ld, t)) {
            const glm::vec3 hit = glm::vec3(m * glm::vec4(lo + ld * t, 1.0f));
            const float dist = glm::length(hit - ray.origin);
            if (dist < bestDist) {
                bestDist = dist;
                best = &node;
            }
        }
    }
    for (const auto& child : node.children()) pickNode(*child, world, ray, best, bestDist, vis);
}

// Cuadricula del suelo estilo C4D: una linea cada 100 cm (0.5 unidades, ver
// kCmPerUnit en MainUi.cpp), algo mas clara que el fondo, y los dos ejes del
// mundo mas oscuros.
std::vector<iron3d::Vertex> buildGridVertices() {
    std::vector<iron3d::Vertex> v;
    const int half = 20;          // 20 lineas a cada lado = +-2000 cm
    const float step = 0.5f;
    const float ext = half * step;
    const glm::vec3 minor(0.42f), major(0.47f), axis(0.26f), up(0.0f, 1.0f, 0.0f);
    for (int i = -half; i <= half; ++i) {
        const float c = i * step;
        const glm::vec3 col = i == 0 ? axis : ((i % 10) == 0 ? major : minor);
        v.push_back({glm::vec3(c, 0.0f, -ext), col, up});
        v.push_back({glm::vec3(c, 0.0f, ext), col, up});
        v.push_back({glm::vec3(-ext, 0.0f, c), col, up});
        v.push_back({glm::vec3(ext, 0.0f, c), col, up});
    }
    return v;
}

// En las vistas ortograficas la rejilla va en el plano de la vista, como en
// C4D: XY en Frontal/Trasera, YZ en Derecha/Izquierda, XZ en el resto.
glm::mat4 gridModelFor(iron3d::OrbitalCamera::View v) {
    using V = iron3d::OrbitalCamera::View;
    switch (v) {
        case V::Front:
        case V::Back: return glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(1.0f, 0.0f, 0.0f));
        case V::Right:
        case V::Left: return glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f));
        default: return glm::mat4(1.0f);
    }
}

} // namespace

int main(int argc, char** argv) {
    using namespace iron3d;

    // --- Preferencias (Fase 3c): que backend usar -- ver core/Preferences.h.
    // Se lee ANTES de crear la ventana (el backend decide los hints de
    // GLFW); el archivo vive junto al ejecutable, igual que la carpeta
    // scenes/. Si no existe o no parsea, loadPreferences devuelve el default
    // (Vulkan) sin tocar nada.
    const std::filesystem::path prefsPath = exeDirectory() / "preferences.cfg";
    Preferences prefs = loadPreferences(prefsPath);
    RendererBackend backend = prefs.renderBackend;

    // Argumentos: "--ui-preset=<id>" fuerza un preset integrado solo para esta
    // sesion (no se guarda); "--icon-sheet" abre la hoja con todos los iconos;
    // "--prefs" abre Preferencias. Las tres son para verificar la UI. El primer argumento que no empiece por
    // "--" es la escena a abrir, como siempre.
    const char* sceneArg = nullptr;
    std::string loadPresetArg; // "--load-preset=<ruta>": con un .lib4d, carga ese modelo (verificacion)
    // "--frame=<nombre>" encuadra ese objeto y "--select=<nombre>" lo deja
    // seleccionado (verificacion de la flecha de objeto fuera de vista).
    std::string frameArg, selectArg;
    std::vector<std::string> uiCmdArgs; // "--ui-cmd=make-editable|frame-selected|commander[:texto]|vp-menu"
    bool travelArg = false; // "--travel": como pulsar la flecha (viaje hacia el seleccionado)
    bool savePresetArg = false; // "--save-preset": abre "Guardar como Preestablecido" con el seleccionado
    bool iconSheet = false;
    // "--no-focus": la ventana sale sin quitar el foco a la que se este usando
    // (capturas de verificacion mientras Core trabaja en otra cosa).
    bool noFocus = false;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--no-focus") {
            noFocus = true;
        } else if (a == "--icon-sheet") {
            iconSheet = true;
        } else if (a == "--prefs") {
            openPreferencesDialog();
        } else if (a.rfind("--load-preset=", 0) == 0) {
            loadPresetArg = a.substr(14);
        } else if (a == "--travel") {
            travelArg = true;
        } else if (a == "--save-preset") {
            savePresetArg = true;
        } else if (a.rfind("--frame=", 0) == 0) {
            frameArg = a.substr(8);
        } else if (a.rfind("--select=", 0) == 0) {
            selectArg = a.substr(9);
        } else if (a.rfind("--ui-cmd=", 0) == 0) {
            uiCmdArgs.push_back(a.substr(9));
        } else if (a.rfind("--ui-preset=", 0) == 0) {
            if (const UiPreset* p = findBuiltinPreset(a.substr(12))) prefs.uiPreset = *p;
            else std::fprintf(stderr, "[Iron3D] Preset de interfaz desconocido: %s\n", a.c_str());
        } else if (!sceneArg && a.rfind("--", 0) != 0) {
            sceneArg = argv[i];
        }
    }

    glfwSetErrorCallback(glfwErrorCallback);
    if (!glfwInit()) {
        std::fprintf(stderr, "[Iron3D] glfwInit fallo.\n");
        return 1;
    }

    configureWindowHints(backend);
    // La interfaz de C4D esta pensada para pantalla completa: se abre maximizada.
    glfwWindowHint(GLFW_MAXIMIZED, GLFW_TRUE);
    if (noFocus) {
        glfwWindowHint(GLFW_FOCUSED, GLFW_FALSE);
        glfwWindowHint(GLFW_FOCUS_ON_SHOW, GLFW_FALSE);
    }
    GLFWwindow* window = glfwCreateWindow(1600, 900, "Iron3D Studio", nullptr, nullptr);
    if (!window) {
        std::fprintf(stderr, "[Iron3D] glfwCreateWindow fallo.\n");
        return 1;
    }
    glfwSetScrollCallback(window, glfwScrollCallback);
    glfwSetDropCallback(window, glfwDropCallback);

    // --- Dear ImGui: el contexto se crea ANTES del renderer (init() de cada
    // backend llama a ImGui_ImplGlfw_InitFor*/ImGui_Impl<Backend>_Init, que
    // necesitan el contexto ya existente). ------------------------------
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    // Sin navegacion de ImGui por teclado: en C4D las teclas son atajos de
    // herramientas (E/R/T, H, O, F/G...), no un foco que salta entre
    // widgets; con ella activa, teclas como Alt o Espacio movian el foco al
    // menu o activaban el widget enfocado por sorpresa.
    io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableKeyboard;
    iron3d::applyPreset(prefs.uiPreset);

    int fbWidth = 0, fbHeight = 0;
    glfwGetFramebufferSize(window, &fbWidth, &fbHeight);

    std::unique_ptr<IRenderer> renderer = createRenderer(backend);
    renderer->init(window, fbWidth, fbHeight);
    // Miniaturas del Navegador de Contenido y librerias permanentes
    // (<Iron3D>/library/browser/*.lib4d).
    iron3d::setUiTextureFactory(
        [&renderer](const uint8_t* rgba, int w, int h) { return renderer->createUiTexture(rgba, w, h); },
        [&renderer](void* tex) { renderer->destroyUiTexture(tex); });
    iron3d::scanPermanentLibraries();
    // Gris neutro #5A5A5A: confirmado en capturas reales de C4D R19 que el
    // viewport es MAS CLARO que los paneles de alrededor (#3C3C3C), no mas
    // oscuro como tenia Iron3D antes -- ver c4d_ui_reference_visual.md SS2.
    // Ahora sale del preset de interfaz (theme.viewportBg; el C4D es ese gris).
    {
        const glm::vec3& bg = prefs.uiPreset.theme.viewportBg;
        renderer->setClearColor(bg.x, bg.y, bg.z, 1.0f);
    }

    // --- Geometria --------------------------------------------------------
    SceneMeshes meshes;
    meshes.renderer = renderer.get();
    meshes.cube = renderer->createMesh(makeCubeMesh());
    meshes.sphere = renderer->createMesh(makeSphereMesh());
    meshes.plane = renderer->createMesh(makePlaneMesh());

    TransformGizmo gizmo;
    std::vector<Vertex> gizmoLineVerts, gizmoTriVerts;
    const std::vector<Vertex> gridVerts = buildGridVertices();
    // Rectangulo del visor: lo decide la UI en cada frame; la camara y el
    // picking usan el del frame anterior (un frame de retraso, invisible).
    ViewportState viewport;
    bool prevLeftDown = false;
    glm::vec3 dragPivot(0.0f); // centro de la seleccion al empezar un arrastre

    OrbitalCamera camera;

    // --- Grafo de escena (Fase 2) --------------------------------------------
    SceneNode sceneRoot("Escena");
    SceneNode* cubeNode = sceneRoot.addChild("Cubo", PrimitiveType::Cube);
    SceneNode* selected = cubeNode;
    std::vector<Material> materials; // biblioteca de materiales (Fase 5), ver core/Material.h
    SceneHistory history(materials); // deshacer/rehacer (Fase 2d), ver SceneHistory.h

    // "Iron3DStudio.exe escena.iron" abre esa escena al arrancar (doble clic
    // en un .iron desde el Explorador, igual que un .c4d con Cinema 4D).
    // Un .c4d tambien (2026-11-04): se importa con Cineware sobre una escena vacia.
    if (sceneArg) {
        std::string err;
        const std::filesystem::path argPath = std::filesystem::path(sceneArg);
        if (iron3d::isLib4dPath(argPath)) {
            // Una .lib4d se monta en el Navegador de Contenido (temporal) y se
            // abre su ventana; la escena sigue siendo la de siempre.
            if (iron3d::mountTemporaryLibrary(argPath, &err)) {
                iron3d::openContentBrowser();
                const iron3d::PresetLibrary* lib = iron3d::findMountedLibrary(argPath);
                for (const iron3d::PresetItem& it : lib ? lib->items() : std::vector<iron3d::PresetItem>{}) {
                    if (loadPresetArg.empty() || it.path != loadPresetArg) continue;
                    iron3d::loadPresetIntoScene(*lib, it, sceneRoot, selected, history, materials);
                    iron3d::frameScene(camera, sceneRoot);
                    std::fprintf(stderr, "[Iron3D] Preestablecido '%s' cargado: %d objetos en la escena\n",
                                 it.name.c_str(), (int)sceneRoot.children().size());
                }
            }
            else std::fprintf(stderr, "[Iron3D] No se pudo abrir la librería '%s': %s\n", sceneArg, err.c_str());
        } else if (iron3d::isC4DPath(argPath)) {
            iron3d::C4DImportReport report;
            sceneRoot.clearChildren();
            if (iron3d::importC4D(argPath, sceneRoot, materials, report, &err)) {
                selected = nullptr;
                iron3d::showImportReport(report);
                iron3d::frameScene(camera, sceneRoot);
            } else {
                std::fprintf(stderr, "[Iron3D] No se pudo importar '%s': %s\n", sceneArg, err.c_str());
                selected = sceneRoot.addChild("Cubo", PrimitiveType::Cube); // el cubo de antes ya no existe
            }
        } else if (loadScene(argPath, sceneRoot, materials, &err)) {
            selected = nullptr;
            iron3d::frameScene(camera, sceneRoot);
        } else {
            std::fprintf(stderr, "[Iron3D] No se pudo abrir '%s': %s\n", sceneArg, err.c_str());
        }
    }
    if (!frameArg.empty() || !selectArg.empty()) {
        std::function<SceneNode*(SceneNode&, const std::string&)> byName = [&](SceneNode& n, const std::string& name) -> SceneNode* {
            if (n.name() == name) return &n;
            for (const auto& c : n.children())
                if (SceneNode* f = byName(*c, name)) return f;
            return nullptr;
        };
        if (SceneNode* f = frameArg.empty() ? nullptr : byName(sceneRoot, frameArg)) iron3d::frameNode(camera, *f);
        if (SceneNode* s = selectArg.empty() ? nullptr : byName(sceneRoot, selectArg)) selected = s;
        if (travelArg && selected) iron3d::travelToNode(camera, *selected);
        if (savePresetArg && selected) iron3d::beginSavePreset(*selected, materials);
    }
    for (const std::string& c : uiCmdArgs)
        if (!iron3d::queueUiCommand(c)) std::fprintf(stderr, "[Iron3D] --ui-cmd desconocida: %s\n", c.c_str());

    // --- Carpeta de escenas (Fase 2c: guardar/cargar .iron) -------------------
    const std::filesystem::path scenesDir = exeDirectory() / "scenes";
    std::filesystem::create_directories(scenesDir);

    // --- Bucle principal -----------------------------------------------------
    double lastMouseX = 0.0, lastMouseY = 0.0;
    glfwGetCursorPos(window, &lastMouseX, &lastMouseY);
    camera.resetMouseDelta(lastMouseX, lastMouseY);

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        if (!g_droppedFiles.empty()) {
            // Una .lib4d se monta en el Navegador de Contenido (como arrastrarla
            // a C4D); el resto se anade a la libreria abierta si se suelta
            // encima del navegador.
            std::vector<std::filesystem::path> rest;
            for (const auto& f : g_droppedFiles) {
                std::string err;
                if (!iron3d::isLib4dPath(f)) rest.push_back(f);
                else if (iron3d::mountTemporaryLibrary(f, &err)) iron3d::openContentBrowser();
                else std::fprintf(stderr, "[Iron3D] No se pudo abrir la librería: %s\n", err.c_str());
            }
            if (!rest.empty()) iron3d::contentBrowserDropFiles(rest);
            g_droppedFiles.clear();
        }

        glfwGetFramebufferSize(window, &fbWidth, &fbHeight);
        renderer->resizeIfNeeded(fbWidth, fbHeight);
        if (fbWidth <= 0 || fbHeight <= 0) {
            continue; // minimizada: no hay nada que dibujar este frame
        }

        double mouseX = 0.0, mouseY = 0.0;
        glfwGetCursorPos(window, &mouseX, &mouseY);
        const bool altHeld = glfwGetKey(window, GLFW_KEY_LEFT_ALT) == GLFW_PRESS ||
                              glfwGetKey(window, GLFW_KEY_RIGHT_ALT) == GLFW_PRESS;
        const bool mouseInViewport = viewport.w > 1.0f && mouseX >= viewport.x && mouseY >= viewport.y &&
                                     mouseX < viewport.x + viewport.w && mouseY < viewport.y + viewport.h;
        if (!io.WantCaptureMouse && mouseInViewport) {
            // Cualquier clic o rueda en el visor descarta la vista renderizada.
            if (viewport.renderView && (g_scrollY != 0.0 ||
                                        glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS ||
                                        glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS ||
                                        glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_MIDDLE) == GLFW_PRESS))
                viewport.renderView = false;
            camera.handleInput(window, mouseX, mouseY, g_scrollY);
        } else {
            camera.resetMouseDelta(mouseX, mouseY); // evita un salto al soltar un panel de ImGui
        }
        g_scrollY = 0.0;

        const float aspect = viewport.w > 1.0f && viewport.h > 1.0f ? viewport.w / viewport.h
                                                                     : (float)fbWidth / (float)fbHeight;
        const glm::mat4 view = camera.viewMatrix();
        const glm::mat4 proj = camera.projectionMatrix(aspect, renderer->needsProjectionYFlip());
        const glm::vec3 eye = camera.eyePosition();
        const glm::mat4 viewProj = proj * view;

        renderer->imguiNewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // Picking/arrastre del gizmo DESPUES de ImGui::NewFrame(): io.WantCaptureMouse
        // se recalcula dentro de NewFrame() contra la posicion actual del raton y el
        // layout del frame anterior, asi que leerlo aqui (y no antes, como estaba)
        // evita usar el valor obsoleto del frame anterior -- un valor viejo podia
        // dejar pasar al gizmo un clic que en realidad caia sobre un boton de la
        // barra de herramientas (causa real de un salto de angulo espurio, 28 oct).
        const bool leftDown = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
        const bool leftPressed = leftDown && !prevLeftDown;
        prevLeftDown = leftDown;        // Con "Seleccion en Vivo" no hay gizmo: el clic solo selecciona.
        SceneNode* gizmoTarget = viewport.selectTool ? nullptr : selected;
        // Gizmo en el centro con varios objetos seleccionados (congelado
        // mientras se arrastra, para que no se escape bajo el cursor).
        gizmo.usePivot = iron3d::selectionCount() > 1;
        if (gizmo.usePivot)
            gizmo.pivotWorld = gizmo.isDragging() ? dragPivot : iron3d::selectionPivot(sceneRoot, selected);
        if (!io.WantCaptureMouse && !altHeld && (mouseInViewport || gizmo.isDragging())) {
            const Ray mouseRay = TransformGizmo::screenRay(mouseX - viewport.x, mouseY - viewport.y,
                                                           (int)viewport.w, (int)viewport.h, view, proj, eye);
            if (leftDown) {
                if (gizmo.isDragging()) {
                    // Con varios objetos seleccionados, cada paso del arrastre
                    // del activo se reparte al resto alrededor del pivote comun.
                    const glm::vec3 beforePos = selected ? glm::vec3(selected->worldMatrix()[3]) : glm::vec3(0.0f);
                    const glm::vec3 beforeRot = selected ? selected->rotationDeg : glm::vec3(0.0f);
                    const glm::vec3 beforeScl = selected ? selected->scale : glm::vec3(1.0f);
                    gizmo.updateDrag(mouseRay, mouseX - viewport.x, mouseY - viewport.y);
                    if (selected && iron3d::selectionCount() > 1) {
                        glm::vec3 ratio(1.0f);
                        for (int k = 0; k < 3; ++k)
                            if (std::fabs(beforeScl[k]) > 1e-6f) ratio[k] = selected->scale[k] / beforeScl[k];
                        iron3d::applySelectionDrag(sceneRoot, selected, dragPivot,
                                                   glm::vec3(selected->worldMatrix()[3]) - beforePos,
                                                   selected->rotationDeg - beforeRot, ratio);
                    }
                } else if (leftPressed) {
                    // Snapshot ANTES de intentar el drag: si agarra un eje de
                    // verdad (beginDrag devuelve true) se confirma como paso
                    // de deshacer; si no agarra nada, el clic selecciona el
                    // objeto bajo el raton (o deselecciona si no hay ninguno).
                    const std::string preDragSnap = history.snapshot(sceneRoot);
                    dragPivot = iron3d::selectionPivot(sceneRoot, selected);
                    // Base de camara en espacio de mundo (filas de la rotacion
                    // de `view`), solo para el modo Free/trackball del gizmo.
                    const glm::vec3 camRight(view[0][0], view[1][0], view[2][0]);
                    const glm::vec3 camUp(view[0][1], view[1][1], view[2][1]);
                    if (gizmo.beginDrag(mouseRay, gizmoTarget, eye, mouseX - viewport.x, mouseY - viewport.y,
                                        (int)viewport.w, (int)viewport.h, viewProj, camRight, camUp)) {
                        history.commit(preDragSnap);
                    } else if (!(viewport.selectTool && viewport.selectShape != 0)) {
                        // (Rectangular y Lazo seleccionan al soltar, en MainUi.)
                        SceneNode* hit = nullptr;
                        float bestDist = 1e30f;
                        for (const auto& child : sceneRoot.children())
                            pickNode(*child, glm::mat4(1.0f), mouseRay, hit, bestDist);
                        // Mayus+clic: anade o quita de la seleccion (C4D).
                        const bool shiftHeld = glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ||
                                               glfwGetKey(window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS;
                        if (shiftHeld) selected = iron3d::toggleViewportSelection(sceneRoot, hit, selected);
                        else selected = hit;
                    }
                }
            } else {
                gizmo.endDrag();
                gizmo.updateHover(mouseRay, gizmoTarget, eye, mouseX - viewport.x, mouseY - viewport.y,
                                  (int)viewport.w, (int)viewport.h, viewProj);
            }
        } else {
            gizmo.endDrag();
        }

        if (iconSheet)
            drawIconSheet(viewport);
        else
            drawMainUi(sceneRoot, selected, gizmo.mode, scenesDir, history, io.Framerate, backend, prefs, prefsPath,
                       materials, camera, viewport);
        ImGui::Render();
        if (viewport.requestQuit) glfwSetWindowShouldClose(window, GLFW_TRUE);

        gizmo.buildGeometry(viewport.selectTool ? nullptr : selected, eye, gizmoLineVerts, gizmoTriVerts);

        // Rectangulo del visor en pixeles de framebuffer (puede diferir de las
        // coordenadas de ventana con escalado DPI).
        const float sx = io.DisplaySize.x > 0.0f ? (float)fbWidth / io.DisplaySize.x : 1.0f;
        const float sy = io.DisplaySize.y > 0.0f ? (float)fbHeight / io.DisplaySize.y : 1.0f;
        // Paneles a dibujar: uno (el visor) o cuatro con F5, cada uno con su
        // camara; el activo usa la `camera` de siempre y lleva el gizmo.
        struct Panel {
            int x, y, w, h;
            glm::mat4 view, proj;
            glm::vec3 eye;
            OrbitalCamera::View kind;
            bool active;
        };
        std::vector<Panel> panels;
        if (viewport.quad) {
            renderer->setViewportRect((int)(viewport.fullX * sx), (int)(viewport.fullY * sy),
                                      (int)(viewport.fullW * sx), (int)(viewport.fullH * sy));
            for (int i = 0; i < 4; ++i) {
                const QuadRect r = quadPanelRect(viewport, i);
                const bool act = i == viewport.quadActive;
                const OrbitalCamera& cam = act ? camera : viewport.quadCams[i];
                const float pa = r.h > 1.0f ? r.w / r.h : aspect;
                panels.push_back({(int)(r.x * sx), (int)(r.y * sy), (int)(r.w * sx), (int)(r.h * sy),
                                  act ? view : cam.viewMatrix(),
                                  act ? proj : cam.projectionMatrix(pa, renderer->needsProjectionYFlip()),
                                  act ? eye : cam.eyePosition(), cam.view(), act});
            }
        } else {
            renderer->setViewportRect((int)(viewport.x * sx), (int)(viewport.y * sy), (int)(viewport.w * sx),
                                      (int)(viewport.h * sy));
            panels.push_back({(int)(viewport.x * sx), (int)(viewport.y * sy), (int)(viewport.w * sx),
                              (int)(viewport.h * sy), view, proj, eye, camera.view(), true});
        }

        if (viewport.renderView) renderer->setClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        else {
            const glm::vec3& bg = prefs.uiPreset.theme.viewportBg;
            renderer->setClearColor(bg.x, bg.y, bg.z, 1.0f);
        }
        if (renderer->beginFrame()) {
            // Luz de la escena (Fase 5): la primera que haya en el grafo; si no
            // hay ninguna, foco de camara de respaldo (headlamp) para que una
            // escena nueva sin Luz no se quede a oscuras.
            glm::vec3 lightPos = eye;
            findFirstLightWorldPos(sceneRoot, glm::mat4(1.0f), lightPos);
            const glm::mat4 lightViewProj = computeLightViewProj(lightPos);

            // Pasada de sombras (Fase render 2): ANTES del render pass
            // principal -- en Vulkan ambas pasadas comparten el mismo command
            // buffer y no se puede anidar/cambiar de render pass sin cerrar el
            // actual (ver SwapchainWindow::beginFrame()/beginRenderPass()).
            renderer->beginShadowPass(lightViewProj);
            drawSceneShadow(*renderer, meshes, sceneRoot, glm::mat4(1.0f));
            renderer->endShadowPass();

            // Pasada de G-buffer para SSAO (Fase 3): mismo motivo que la de
            // sombras arriba -- tiene que cerrarse antes de abrir el render
            // pass principal. Usa la vista/proyeccion de la camara real (no
            // las de la luz), asi que se recorre DESPUES de calcular `view`/
            // `proj` mas arriba en este mismo bucle.
            // Con 4 paneles el G-buffer (una sola camara, pantalla completa)
            // no vale para los cuatro: se deja vacio y el SSAO queda en 1
            // (sin oclusion), ver ssao.frag.
            renderer->beginGBufferPass(view, proj);
            if (!viewport.quad) drawSceneGBuffer(*renderer, meshes, sceneRoot, glm::mat4(1.0f));
            renderer->endGBufferPass();

            renderer->setLighting(lightPos, eye, lightViewProj);
            renderer->beginMainPass();

            for (const Panel& pn : panels) {
                renderer->setDrawRect(pn.x, pn.y, pn.w, pn.h);
                renderer->setLighting(lightPos, pn.eye, lightViewProj);
                const glm::mat4 pvp = pn.proj * pn.view;
                if (viewport.showGrid && !viewport.renderView)
                    renderer->drawGrid(gridVerts, pvp * gridModelFor(pn.kind));
                drawScene(*renderer, meshes, materials, sceneRoot, glm::mat4(1.0f), pvp);

                // Gizmo encima de la escena (ningun buffer de profundidad todavia en
                // Fase 1-2, asi que "despues" ya implica "por encima"). Solo en
                // el panel activo: sus vertices dependen de su camara.
                if (pn.active && !viewport.renderView) renderer->drawGizmo(gizmoLineVerts, gizmoTriVerts, viewProj);
            }

            renderer->endFrame(); // compone ImGui y presenta
        }
        meshes.collect(); // libera en GPU las mallas que ya nadie usa
    }

    // --- Cierre ----------------------------------------------------------------
    // No hace falta destroyMesh() aqui: renderer->shutdown() ya destruye todas
    // las mallas que tiene registradas (y lo hace tras su propio
    // vkDeviceWaitIdle en el backend Vulkan). Llamarlo antes, como se hacia,
    // destruia los VkBuffer mientras el ultimo frame enviado a la GPU
    // todavia los estaba usando -- validation error + crash en vez de cierre
    // limpio.
    iron3d::releaseContentBrowserTextures();
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
