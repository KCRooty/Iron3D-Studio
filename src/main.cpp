// Iron3D Studio -- Fase 1 (Documento Maestro §8.1 punto 1):
//   "Esqueleto CMake, fase 1: ventana GLFW/SDL2 con ImGui; contexto Vulkan,
//    VMA y mallas basicas con camara orbital estilo C4D."
//
// Fase 3: main.cpp ya no sabe nada de Vulkan directamente -- todo el render
// pasa por IRenderer (ver gfx/IRenderer.h), que VulkanRenderer implementa
// envolviendo exactamente lo que este archivo hacia a mano hasta la Fase 2d.
// Permite anadir un backend OpenGL (Fase 3b) sin tocar esta logica.

#define GLFW_INCLUDE_NONE
#if defined(_WIN32)
extern "C" __declspec(dllimport) int __stdcall MessageBoxW(void* hWnd, const wchar_t* text, const wchar_t* caption,
                                                         unsigned int type);
#endif
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
            // Etiqueta de Textura del nodo: solo proyeccion UVW (6), como el
            // render por CPU; las demas proyecciones salen con color plano.
            auto mapping = [](const iron3d::TextureTagData& t) {
                iron3d::TexMapping tex;
                tex.enabled = t.projection == 6;
                tex.offset = t.offset;
                tex.length = t.length;
                tex.tile = t.tile;
                return tex;
            };
            if (h == iron3d::kInvalidMesh) break;
            const glm::mat4 mvp = viewProj * world;
            // Partes con otro material (Selecciones de Poligonos de C4D): cada
            // rango con el suyo y los huecos con el del objeto. Si la malla ha
            // cambiado y los rangos ya no caben, el objeto entero con el suyo.
            const uint32_t total = node.mesh ? (uint32_t)node.mesh->indices.size() : 0u;
            bool partsOk = node.primitive == PrimitiveType::Mesh && !node.materialParts.empty() && total > 0;
            uint32_t end = 0;
            for (const iron3d::MaterialPart& part : node.materialParts) {
                partsOk = partsOk && part.firstIndex >= end && part.firstIndex + part.indexCount <= total;
                end = part.firstIndex + part.indexCount;
            }
            if (!partsOk) {
                renderer.drawMesh(h, world, mvp, material, mapping(node.texTag));
                break;
            }
            uint32_t cur = 0;
            for (const iron3d::MaterialPart& part : node.materialParts) {
                if (part.firstIndex > cur)
                    renderer.drawMesh(h, world, mvp, material, mapping(node.texTag), cur, part.firstIndex - cur);
                if (part.indexCount > 0)
                    renderer.drawMesh(h, world, mvp, iron3d::materialOrDefault(materials, part.materialIndex),
                                      mapping(part.texTag), part.firstIndex, part.indexCount);
                cur = part.firstIndex + part.indexCount;
            }
            if (cur < total) renderer.drawMesh(h, world, mvp, material, mapping(node.texTag), cur, total - cur);
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
        case PrimitiveType::Camera: {
            // Marcador como la Luz, con un cubo (con su rotacion: se ve hacia donde mira).
            const glm::mat4 iconWorld = world * glm::scale(glm::mat4(1.0f), 0.15f / glm::max(node.scale, glm::vec3(1e-6f)));
            renderer.drawMesh(meshes.cube, iconWorld, viewProj * iconWorld, material);
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
        case PrimitiveType::Camera:
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
        } else if (node.primitive == iron3d::PrimitiveType::Camera) {
            m = world * glm::scale(glm::mat4(1.0f), 0.15f / glm::max(node.scale, glm::vec3(1e-6f)));
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
    bool dumpSceneArg = false; // "--dump-scene": lista los objetos cargados por stderr (verificacion)
    // "--also=<lib>|<ruta>": carga ademas ese preestablecido de otra libreria
    // (verificacion: un personaje con un pack de luces encima).
    std::vector<std::string> alsoPresetArgs;
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
        } else if (a.rfind("--also=", 0) == 0) {
            alsoPresetArgs.push_back(a.substr(7));
        } else if (a == "--dump-scene") {
            dumpSceneArg = true;
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

    // Sin Vulkan utilizable (drivers viejos, GPU integrada antigua, maquina
    // virtual): OpenGL para esta sesion, sin tocar preferences.cfg.
    if (backend == RendererBackend::Vulkan && !vulkanUsable()) {
        std::fprintf(stderr, "[Iron3D] Vulkan no esta disponible en este equipo: se usa OpenGL.\n");
        backend = RendererBackend::OpenGL;
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
#if defined(_WIN32)
        MessageBoxW(nullptr,
                    L"Iron3D Studio no ha podido abrir su ventana.\n\n"
                    L"Necesita una tarjeta gr\u00e1fica con Vulkan 1.3 u OpenGL 3.3. "
                    L"Prueba a actualizar los drivers de la tarjeta gr\u00e1fica.",
                    L"Iron3D Studio", 0x10 /* MB_ICONERROR */);
#endif
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
    // Clic con Mover/Rotar/Escalar fuera de los handles: selecciona y deja un
    // arrastre Free pendiente. Hasta pasar de 3 px no mueve nada ni crea paso
    // de deshacer; si se suelta antes, es un clic (y en vacio deselecciona).
    bool clickDragPending = false;
    bool clickDeselect = false;
    bool clickComp = false;
    std::string clickSnap;
    double pressX = 0.0, pressY = 0.0;
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
                // Abrir un modelo de la libreria = escena con ese modelo, sin el
                // Cubo de la escena inicial (C4D tampoco lo tiene).
                if (!loadPresetArg.empty()) {
                    sceneRoot.clearChildren();
                    selected = nullptr;
                }
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
    for (const std::string& also : alsoPresetArgs) {
        const size_t bar = also.find('|');
        if (bar == std::string::npos) continue;
        const std::filesystem::path libPath(std::u8string(reinterpret_cast<const char8_t*>(also.data()), bar));
        const std::string itemPath = also.substr(bar + 1);
        std::string err;
        if (!iron3d::mountTemporaryLibrary(libPath, &err)) {
            std::fprintf(stderr, "[Iron3D] --also: no se pudo abrir '%s': %s\n", also.c_str(), err.c_str());
            continue;
        }
        const iron3d::PresetLibrary* lib = iron3d::findMountedLibrary(libPath);
        bool found = false;
        for (const iron3d::PresetItem& it : lib ? lib->items() : std::vector<iron3d::PresetItem>{})
            if (it.path == itemPath) {
                iron3d::loadPresetIntoScene(*lib, it, sceneRoot, selected, history, materials);
                found = true;
            }
        if (!found)
            std::fprintf(stderr, "[Iron3D] --also: '%s' no esta en la libreria (%s)\n", itemPath.c_str(),
                         lib ? "montada" : "no encontrada tras montarla");
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
    // "--dump-scene" (verificacion, 2026-11-10): una linea por objeto con
    // geometria -- caja en mundo, visibilidad, material y textura de Color --
    // para diagnosticar modelos importados que no se ven como en C4D.
    if (dumpSceneArg) {
        std::function<void(const SceneNode&, const glm::mat4&, bool, int)> dump =
            [&](const SceneNode& n, const glm::mat4& parent, bool parentVis, int depth) {
                const glm::mat4 w = parent * n.localMatrix();
                const bool vis = iron3d::resolveVisibility(n.editorVis, parentVis);
                if (n.primitive != PrimitiveType::None) {
                    glm::vec3 lo(-0.5f), hi(0.5f);
                    if (n.primitive == PrimitiveType::Mesh && n.mesh) lo = n.mesh->boundsMin, hi = n.mesh->boundsMax;
                    glm::vec3 mn(1e30f), mx(-1e30f);
                    for (int i = 0; i < 8; ++i) {
                        const glm::vec3 c((i & 1) ? hi.x : lo.x, (i & 2) ? hi.y : lo.y, (i & 4) ? hi.z : lo.z);
                        const glm::vec3 p(w * glm::vec4(c, 1.0f));
                        mn = glm::min(mn, p);
                        mx = glm::max(mx, p);
                    }
                    const iron3d::Material& m = iron3d::materialOrDefault(materials, n.materialIndex);
                    const std::string& tex = m.texture(iron3d::TexSlot::Color);
                    const bool found = !tex.empty() && !iron3d::resolveTexturePath(tex, m.texBaseDir).empty();
                    std::fprintf(stderr,
                                 "[dump] %*s%s prim=%d vis=%d pts=%zu caja=(%.1f %.1f %.1f)-(%.1f %.1f %.1f) mat=%d '%s' "
                                 "color=(%.2f %.2f %.2f) tex='%s'%s proy=%d\n",
                                 depth * 2, "", n.name().c_str(), (int)n.primitive, (int)vis,
                                 n.mesh ? n.mesh->vertices.size() : (size_t)0, mn.x, mn.y, mn.z, mx.x, mx.y, mx.z,
                                 n.materialIndex, m.name.c_str(), m.baseColor.r, m.baseColor.g, m.baseColor.b,
                                 tex.c_str(), tex.empty() ? "" : (found ? " (ok)" : " (NO ENCONTRADA)"),
                                 n.texTag.projection);
                } else {
                    std::fprintf(stderr, "[dump] %*s%s (nulo) vis=%d\n", depth * 2, "", n.name().c_str(), (int)vis);
                }
                for (const auto& c : n.children()) dump(*c, w, vis, depth + 1);
            };
        for (const auto& c : sceneRoot.children()) dump(*c, glm::mat4(1.0f), true, 0);
        // Luces tal como llegan al visor (las que iluminan de verdad).
        const iron3d::SceneLighting sl = iron3d::collectSceneLights(sceneRoot);
        std::fprintf(stderr, "[dump] luces en el visor: %zu (sombra de la n.%d)\n", sl.lights.size(), sl.shadowLight);
        for (size_t i = 0; i < sl.lights.size(); ++i) {
            const iron3d::ViewportLight& v = sl.lights[i];
            std::fprintf(stderr,
                         "[dump]   luz %zu tipo=%d pos=(%.2f %.2f %.2f) dir=(%.2f %.2f %.2f) color=(%.2f %.2f %.2f) "
                         "cos=(%.3f %.3f) caida=%d radio=%.2f dif=%d esp=%d\n",
                         i, (int)v.type, v.position.x, v.position.y, v.position.z, v.direction.x, v.direction.y,
                         v.direction.z, v.color.r, v.color.g, v.color.b, v.cosOuter, v.cosInner, v.falloff, v.radius,
                         (int)v.diffuse, (int)v.specular);
        }
    }

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
        gizmo.worldAxes = viewport.worldCoords;
        // Puntos/Aristas/Poligonos: el gizmo mueve los componentes seleccionados.
        SceneNode* compProxy = viewport.selectTool ? nullptr : iron3d::componentGizmoTarget(selected, gizmo.isDragging());
        if (compProxy) {
            gizmoTarget = compProxy;
            gizmo.usePivot = false;
        }
        // Extruir / Biselar / Cuchillo: sin gizmo, el raton es de la herramienta.
        const bool modelTool = iron3d::modelToolActive() && !gizmo.isDragging();
        if (modelTool) {
            gizmoTarget = nullptr;
            compProxy = nullptr;
        }
        if (gizmo.usePivot)
            gizmo.pivotWorld = gizmo.isDragging() ? dragPivot : iron3d::selectionPivot(sceneRoot, selected);
        if (!io.WantCaptureMouse && !altHeld && (mouseInViewport || gizmo.isDragging())) {
            const Ray mouseRay = TransformGizmo::screenRay(mouseX - viewport.x, mouseY - viewport.y,
                                                           (int)viewport.w, (int)viewport.h, view, proj, eye);
            if (modelTool) {
                iron3d::modelToolMouse(sceneRoot, selected, camera, (float)mouseX, (float)mouseY, leftDown, leftPressed,
                                       history);
            } else if (leftDown) {
                if (gizmo.isDragging() && clickDragPending && std::hypot(mouseX - pressX, mouseY - pressY) < 3.0) {
                    // Todavia es un clic: no se mueve nada.
                } else if (gizmo.isDragging()) {
                    if (clickDragPending) {
                        history.commit(clickSnap);
                        clickDragPending = false;
                    }
                    // Con varios objetos seleccionados, cada paso del arrastre
                    // del activo se reparte al resto alrededor del pivote comun.
                    const glm::vec3 beforePos = selected ? glm::vec3(selected->worldMatrix()[3]) : glm::vec3(0.0f);
                    const glm::vec3 beforeRot = selected ? selected->rotationDeg : glm::vec3(0.0f);
                    const glm::vec3 beforeScl = selected ? selected->scale : glm::vec3(1.0f);
                    const glm::mat4 beforeWorld = selected ? selected->worldMatrix() : glm::mat4(1.0f);
                    gizmo.updateDrag(mouseRay, mouseX - viewport.x, mouseY - viewport.y);
                    if (compProxy) {
                        if (selected) iron3d::applyComponentDrag(*selected);
                    } else if (selected && viewport.axisMode) {
                        // L (Activar Eje): solo cambia el eje; geometria e hijos se quedan.
                        iron3d::compensateAxisMove(*selected, beforeWorld);
                    }
                    if (!compProxy && selected && iron3d::selectionCount() > 1) {
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
                    const bool shiftHeld = glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ||
                                           glfwGetKey(window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS;
                    const bool ctrlHeld = glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS ||
                                          glfwGetKey(window, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS;
                    const double lx = mouseX - viewport.x, ly = mouseY - viewport.y;
                    auto startDrag = [&](SceneNode* target, GizmoAxis force) {
                        return gizmo.beginDrag(mouseRay, target, eye, lx, ly, (int)viewport.w, (int)viewport.h,
                                               viewProj, camRight, camUp, force);
                    };
                    // Arrastre Free que empieza como clic (ver clickDragPending).
                    auto startClickDrag = [&](SceneNode* target, bool comp, bool deselectOnClick) {
                        if (!startDrag(target, GizmoAxis::Free)) return false;
                        clickDragPending = true;
                        clickSnap = preDragSnap;
                        clickComp = comp;
                        clickDeselect = deselectOnClick;
                        pressX = mouseX;
                        pressY = mouseY;
                        return true;
                    };
                    // Mover/Rotar/Escalar: el gizmo solo se lleva el clic si cae
                    // en un handle de verdad; fuera de ellos se selecciona primero
                    // y luego se arrastra lo seleccionado (como C4D).
                    const bool transformTool = !viewport.selectTool;
                    const GizmoAxis handle =
                        gizmoTarget ? gizmo.pickHandle(mouseRay, gizmoTarget, eye, lx, ly, (int)viewport.w,
                                                       (int)viewport.h, viewProj)
                                    : GizmoAxis::None;
                    const int compHit = iron3d::componentHit(selected, camera, (float)mouseX, (float)mouseY);
                    if (handle != GizmoAxis::None && startDrag(gizmoTarget, handle)) {
                        history.commit(preDragSnap);
                        if (compProxy && selected) iron3d::beginComponentDrag(*selected);
                    } else if (viewport.selectTool && viewport.selectShape != 0) {
                        // (Rectangular y Lazo seleccionan al soltar, en MainUi.)
                    } else if (compHit != -2) {
                        // Puntos/Aristas/Poligonos: el clic elige componentes.
                        if (!transformTool) {
                            iron3d::componentClick(selected, camera, (float)mouseX, (float)mouseY, shiftHeld, ctrlHeld);
                        } else {
                            // Uno sin seleccionar: se elige (Mayus anade, Ctrl quita).
                            // Uno ya seleccionado: se arrastra la seleccion tal cual.
                            if (compHit >= 0 && (ctrlHeld || !iron3d::componentSelected(compHit)))
                                iron3d::componentClick(selected, camera, (float)mouseX, (float)mouseY, shiftHeld,
                                                       ctrlHeld);
                            SceneNode* proxy = ctrlHeld ? nullptr : iron3d::componentGizmoTarget(selected, false);
                            if (proxy && startClickDrag(proxy, true, compHit < 0 && !shiftHeld))
                                iron3d::beginComponentDrag(*selected);
                            else if (compHit < 0 && !shiftHeld && !ctrlHeld)
                                iron3d::clearComponentSelection();
                        }
                    } else {
                        SceneNode* hit = nullptr;
                        float bestDist = 1e30f;
                        for (const auto& child : sceneRoot.children())
                            pickNode(*child, glm::mat4(1.0f), mouseRay, hit, bestDist);
                        const bool hitSelected = hit && (hit == selected || iron3d::viewportSelectionContains(hit));
                        if (!transformTool) {
                            // Mayus+clic: anade o quita de la seleccion (C4D).
                            if (shiftHeld) selected = iron3d::toggleViewportSelection(sceneRoot, hit, selected);
                            else selected = hit;
                        } else if (ctrlHeld) {
                            // Ctrl+clic: quita ese objeto de la seleccion; no arrastra.
                            if (hitSelected) selected = iron3d::toggleViewportSelection(sceneRoot, hit, selected);
                        } else {
                            if (hit && !hitSelected) {
                                if (shiftHeld) {
                                    selected = iron3d::toggleViewportSelection(sceneRoot, hit, selected);
                                } else {
                                    iron3d::viewportSelectOnly(hit);
                                    selected = hit;
                                }
                            }
                            // Arrastre Free de la seleccion (nueva o la que ya habia).
                            gizmo.usePivot = iron3d::selectionCount() > 1;
                            dragPivot = iron3d::selectionPivot(sceneRoot, selected);
                            if (gizmo.usePivot) gizmo.pivotWorld = dragPivot;
                            if (!(selected && startClickDrag(selected, false, !hit && !shiftHeld)) && !hit &&
                                !shiftHeld)
                                selected = nullptr;
                        }
                    }
                }
            } else {
                if (gizmo.isDragging()) {
                    if (!clickDragPending) iron3d::notifyTransformEdited(sceneRoot);
                    else if (clickDeselect) {
                        // Clic sin arrastre en vacio: deselecciona.
                        if (clickComp) iron3d::clearComponentSelection();
                        else selected = nullptr;
                    }
                }
                clickDragPending = false;
                gizmo.endDrag();
                gizmo.updateHover(mouseRay, gizmoTarget, eye, mouseX - viewport.x, mouseY - viewport.y,
                                  (int)viewport.w, (int)viewport.h, viewProj);
            }
        } else {
            clickDragPending = false;
            gizmo.endDrag();
            if (modelTool)
                iron3d::modelToolMouse(sceneRoot, selected, camera, (float)mouseX, (float)mouseY, false, false, history);
        }

        if (iconSheet)
            drawIconSheet(viewport);
        else
            drawMainUi(sceneRoot, selected, gizmo.mode, scenesDir, history, io.Framerate, backend, prefs, prefsPath,
                       materials, camera, viewport);
        ImGui::Render();
        if (viewport.requestQuit) glfwSetWindowShouldClose(window, GLFW_TRUE);

        gizmo.buildGeometry(viewport.selectTool || modelTool ? nullptr : (compProxy ? compProxy : selected), eye, gizmoLineVerts,
                            gizmoTriVerts);

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
            // Luces de la escena (2026-11-10): todas las visibles, con su tipo,
            // color, intensidad y caida. Sin ninguna, luz por defecto desde la
            // camara (como C4D). El mapa de sombras es de la primera luz que
            // tenga sombra activada.
            const iron3d::SceneLighting sceneLighting = iron3d::collectSceneLights(sceneRoot);
            glm::vec3 shadowPos = eye;
            if (sceneLighting.shadowLight >= 0) {
                const iron3d::ViewportLight& sl = sceneLighting.lights[sceneLighting.shadowLight];
                shadowPos = sl.type == iron3d::ViewportLight::Type::Directional ? -sl.direction * 10.0f : sl.position;
            }
            const glm::mat4 lightViewProj = computeLightViewProj(shadowPos);
            renderer->setSceneLighting(sceneLighting, lightViewProj);

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

            renderer->beginMainPass();

            for (const Panel& pn : panels) {
                renderer->setDrawRect(pn.x, pn.y, pn.w, pn.h);
                renderer->setEyePosition(pn.eye);
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
    iron3d::releaseUiTextures();
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
