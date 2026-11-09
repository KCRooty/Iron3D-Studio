#include "MainUi.h"
#include "C4DIcons.h"
#include "UiPreset.h"
#include "UiTheme.h"
#include "core/SceneSerializer.h"
#include "core/MeshData.h"
#include "core/MeshTopology.h"
#include "core/ThumbnailRenderer.h"

#define STBI_WRITE_NO_STDIO
#include <stb_image_write.h>
#include <array>
#include <chrono>
#include <sstream>
#include <set>
#include <map>
#include <tuple>
#include <ctime>
#include <fstream>
#include "FileDialog.h"
#include "core/Paths.h"
#include "importers/cineware/CinewareImporter.h"
#include "importers/lib4d/PresetLibrary.h"
#include "ContentBrowser.h"

#include <imgui.h>
#include <imgui_internal.h> // MenuItemEx / BeginMenuEx (iconos en los menus)

#include <glm/glm.hpp>

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <functional>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

namespace iron3d {

// Modos Puntos/Aristas/Poligonos (definidas mas abajo, fuera del namespace anonimo).
bool componentMarquee(SceneNode* selected, const OrbitalCamera& camera, const ViewportState& vp,
                      const std::function<bool(ImVec2)>& inside, bool shift, bool ctrl);
bool deleteComponents(SceneNode& root, SceneNode* selected, SceneHistory& history);

namespace {

using icons::Icon;

// --- Paleta (c4d_ui_reference_visual.md SS2) ---------------------------------
// Ya no son constexpr: applyTheme() las recalcula a partir de UiTheme (ver
// UiTheme.h/.cpp) cada vez que Core cambia un color en Preferencias >
// Interfaz. Los valores de abajo son el default C4D, por si applyTheme()
// no llega a llamarse antes de que algo pinte (no deberia pasar).
ImU32 kAmber = IM_COL32(224, 148, 46, 255);
ImU32 kToolBlue = IM_COL32(80, 144, 208, 150);
ImU32 kTabActive = IM_COL32(141, 167, 204, 255);  // pestaña activa (8DA7CC), texto negro
ImU32 kSelOrange = IM_COL32(255, 170, 24, 255);   // selección (FFAA18): nombre y contorno
ImU32 kHoverRow = IM_COL32(255, 255, 255, 14);
ImU32 kTextCol = IM_COL32(216, 216, 216, 255);
ImU32 kTextDim = IM_COL32(128, 128, 128, 255);
ImU32 kBorderDark = IM_COL32(30, 30, 30, 255);
ImU32 kBorderLight = IM_COL32(80, 80, 80, 255);
ImU32 kSectionBg = IM_COL32(50, 50, 50, 255);
ImU32 kStatusBg = IM_COL32(26, 26, 26, 255);
ImU32 kRulerBg = IM_COL32(52, 52, 52, 255);
ImU32 kPlayhead = IM_COL32(86, 170, 96, 255);
// Cromo que antes era literal (pestanas verticales, asa de los paneles,
// franja de iconos): ahora sigue al fondo de paneles del preset.
ImU32 kTabOn = IM_COL32(78, 78, 78, 255);
ImU32 kTabOff = IM_COL32(52, 52, 52, 255);
ImU32 kGrip = IM_COL32(120, 120, 120, 255);
ImU32 kIconGutter = IM_COL32(48, 48, 48, 255);

// Preset de interfaz activo (applyPreset): disposicion y nombres de paneles.
UiPreset g_preset;
const PanelNames* g_names = nullptr;
const PanelNames& names() { return g_names ? *g_names : panelNames(Arrangement::C4D); }
bool isC4DArrangement() { return g_preset.layout.arrangement == Arrangement::C4D; }

// 1 unidad de Iron3D = 200 cm: el Cubo por defecto mide 1 unidad de lado y en
// C4D un Cubo nuevo mide 200 cm, asi los numeros que se ven coinciden.
constexpr float kCmPerUnit = 200.0f;

ImFont* g_fontBold = nullptr;

enum class AttrFocus { Object, Material, Tag, Project };

// Ordenes que se piden desde sitios sin acceso a la escena (barra izquierda,
// menu contextual del visor) y se ejecutan al principio del frame siguiente.
enum class UiCmd { None, MakeEditable, FrameSelected, FrameAll, ResetView, Group, SelectChildren,
                   CurrentState, Connect, ConnectDelete, SelectAll, ViewPersp, ViewTop, ViewRight, ViewFront,
                   ViewLeft, ViewBack, ViewBottom, ViewQuad, ClearAnimation };

struct UiState {
    UiCmd pendingCmd = UiCmd::None;
    std::vector<UiCmd> cmdQueue;  // --ui-cmd (varias seguidas)
    // Seleccion Rectangular / Lazo en curso (puntos en coordenadas de ventana).
    bool marquee = false;
    std::vector<ImVec2> marqueePts;
    bool showCommander = false;  // Mayus+C
    std::string commanderPreset; // texto inicial (solo --ui-cmd=commander:texto)
    int openVpMenu = 0;          // --ui-cmd=vp-menu: frames que faltan para abrirlo
    char commanderBuf[96] = {};
    int commanderSel = 0;
    std::string status;          // aviso corto en la barra de estado (p. ej. "2 objetos editables")
    double statusUntil = 0.0;
    // Ultima herramienta usada (Espacio alterna con ella, como en C4D).
    int lastTool = -1, curTool = -1;
    bool showScene = false;
    bool sceneLoadMode = false;
    bool showPrefs = false;
    bool showMatEditor = false;
    bool showAbout = false;
    // El hueco del Gestor de Objetos muestra el Navegador de Contenido
    // (pestana vertical "Navegador de Contenido", Ventana o Mayus+F8).
    bool browserOpen = false;
    // Viaje de la camara al pulsar la flecha de "objeto fuera de vista":
    // interpola objetivo y distancia en ~0.35 s. Se cancela si otra cosa
    // mueve la camara mientras tanto (Alt+clic, H, etc.).
    struct CamTrip {
        bool active = false;
        float t = 0.0f;
        glm::vec3 fromTarget{0.0f}, toTarget{0.0f};
        float fromDist = 0.0f, toDist = 0.0f;
        glm::vec3 lastTarget{0.0f};
        float lastDist = 0.0f;
    } camTrip;
    // Resultado de Abrir/Fusionar un .c4d (o el error de abrir cualquier archivo).
    bool showImport = false;
    C4DImportReport importReport;
    std::string importError;
    // Resultado de Exportar a Cinema 4D / Guardar Objetos Seleccionados como.
    bool showExport = false;
    C4DExportReport exportReport;
    std::string exportPath;

    AttrFocus focus = AttrFocus::Object;
    int lastSelectedId = -1;
    int selectedMat = -1;
    int matEditorChannel = 0;

    int renamingNodeId = -1;
    int renamingMat = -1;
    char renameBuf[128] = {};
    bool renameFocus = false;

    std::unordered_set<int> collapsed;
    // Seleccion multiple (ids): Mayus+clic en el visor, Ctrl+clic en el Gestor
    // de Objetos. `selected` sigue siendo el objeto activo (el de Atributos y
    // el gizmo); este conjunto siempre lo contiene mientras haya seleccion.
    std::unordered_set<int> selIds;
    // Raton sobre el Gestor de Objetos (frame anterior): Mayus+G expande el
    // grupo ahi y va al final de la animacion en el resto, como en C4D.
    bool objMgrHovered = false;
    unsigned objMask[3] = {1u << 2, 1u << 2, 1u << 2}; // geometria, luz, nulo: "Objeto"/"General"
    unsigned matMask = 1u << 1;                          // material: "Color"
    unsigned tagMask = 1u << 1;                          // etiqueta: "Etiqueta"
    int clipboardNodeId = -1;

    // Proyecto / Linea de Tiempo (seccion 15 de la referencia: FPS 30, 0F-90F).
    int fps = 30;
    int minFrame = 0, maxFrame = 90;
    int previewMin = 0, previewMax = 90;
    float frame = 0.0f;
    int playDir = 0; // 0 parado, 1 adelante, -1 atras
    bool loop = true;

    int coordSpace = 0; // Objeto (Rel)
    int coordSizeMode = 0; // 0 Tamano, 1 Escala
    int layoutPreset = 0;

    std::string hint; // texto de ayuda del elemento bajo el raton (barra de estado)

    // Configuracion de Render (Ctrl+B) y Visor de Imagenes (Mayus+R).
    struct RenderSettings {
        int width = 800, height = 600;  // como el preset por defecto de C4D
        int antialias = 2;              // NxN muestras por pixel (1-4)
        bool useViewportSize = false;
        float bgTop[3] = {0.0f, 0.0f, 0.0f};    // C4D renderiza sobre negro
        float bgBottom[3] = {0.0f, 0.0f, 0.0f};
    } render;
    bool showRenderSettings = false;
    bool pendingWorldCoords = false, pendingAxisMode = false;  // --ui-cmd antes del primer frame
    int pendingCompMode = 0;
    std::vector<int> pendingCompSel;
    bool compDeleteRequested = false;  // --ui-cmd=comp-del
    int compSelectAllRequested = 0;    // --ui-cmd=sel-all-visible|sel-all-through

    // Opciones de herramienta (la rueda dentada de los menus, como en C4D).
    enum class OptWin { None, RectSel, LassoSel, Connect };
    OptWin optWin = OptWin::None;
    ImVec2 optPos{0.0f, 0.0f};
    bool selTolerant = true;       // Seleccion Rectangular/Lazo: tocar basta
    bool selVisibleOnly = true;    // Solo Seleccionar Visibles (componentes tapados no cuentan)
    bool connectWeld = true;       // Conectar Objetos: soldar puntos coincidentes
    float connectWeldCm = 0.01f;   // tolerancia de la soldadura, en cm

    // Animacion (F9): canales que se graban, claves automaticas y ultimo
    // fotograma evaluado (al cambiar de fotograma se recalcula la escena).
    bool keyPos = true, keyRot = true, keyScl = true;
    bool autoKey = false;
    bool recordRequested = false;
    float lastEvalFrame = -1e9f;
    std::vector<int> selKeys;  // claves del objeto activo (para la Linea de Tiempo)

    // Modos Puntos/Aristas/Poligonos: topologia del objeto activo (puntos =
    // vertices de render unidos por posicion; poligonos = triangulos),
    // seleccion de componentes y estado del arrastre.
    struct Components {
        std::shared_ptr<const MeshData> mesh;  // malla de la que se saco la topologia (se retiene: sin punteros reciclados)
        MeshTopology topo;
        // Visibilidad (z-buffer en CPU), en cache mientras no cambien malla, camara ni visor.
        ComponentVisibility vis;
        const MeshData* visMesh = nullptr;
        glm::mat4 visMvp{0.0f};
        int visW = 0, visH = 0;
        int nodeId = -1, mode = 0;
        std::set<int> sel;
        std::unique_ptr<SceneNode> proxy;
        glm::mat4 dragStartProxy{1.0f};
        std::shared_ptr<const MeshData> dragStartMesh;
    } comp;
    bool showPictureViewer = false;
    bool renderRequested = false;
    struct PictureViewer {
        Thumbnail image;
        void* tex = nullptr;
        double seconds = 0.0;
        int aa = 1;
        float zoom = 0.0f;  // 0 = ajustar a la ventana
        std::string viewName;
    } pv;
};
UiState g;
// Visor del frame actual (lo fija drawMainUi): selectView lo necesita desde
// sitios que solo reciben la camara.
ViewportState* g_vp = nullptr;
void flashStatus(const std::string& text);
void collectSelected(SceneNode& node, std::vector<SceneNode*>& out);

// --- Animacion (F9) -------------------------------------------------------------------
// Graba una clave de los objetos seleccionados en el fotograma actual.
int recordActiveObjects(SceneNode& root, SceneNode* selected, SceneHistory* history) {
    std::vector<SceneNode*> nodes;
    collectSelected(root, nodes);
    if (nodes.empty() && selected) nodes.push_back(selected);
    if (nodes.empty()) {
        flashStatus("Grabar Objetos Activos: no hay ningún objeto seleccionado.");
        return 0;
    }
    if (!g.keyPos && !g.keyRot && !g.keyScl) {
        flashStatus("Grabar Objetos Activos: Posición, Escala y Rotación están desactivadas.");
        return 0;
    }
    if (history) history->commitBefore(root);
    const int f = (int)std::lround(g.frame);
    for (SceneNode* n : nodes) recordKey(*n, f, g.keyPos, g.keyRot, g.keyScl);
    g.lastEvalFrame = g.frame;  // los valores actuales ya son los de este fotograma
    flashStatus("Clave grabada en el fotograma " + std::to_string(f) + " (" + std::to_string(nodes.size()) +
                (nodes.size() == 1 ? " objeto)." : " objetos)."));
    return (int)nodes.size();
}

// Ctrl+F / Ctrl+G: clave anterior / siguiente del objeto activo.
void jumpToKey(int dir) {
    const int cur = (int)std::lround(g.frame);
    if (dir < 0) {
        for (auto it = g.selKeys.rbegin(); it != g.selKeys.rend(); ++it)
            if (*it < cur) { g.frame = (float)*it; g.playDir = 0; return; }
    } else {
        for (int k : g.selKeys)
            if (k > cur) { g.frame = (float)k; g.playDir = 0; return; }
    }
}

// Abre la ventanita de opciones de una herramienta junto al raton.
void openToolOptions(int which) {
    g.optWin = (UiState::OptWin)which;
    g.optPos = ImGui::GetIO().MousePos;
}

// --- Vistas y vista de 4 paneles (F1-F5) ----------------------------------------------
using CamView = OrbitalCamera::View;
constexpr CamView kQuadViews[4] = {CamView::Perspective, CamView::Top, CamView::Right, CamView::Front};

int quadPanelOf(CamView v) {
    for (int i = 0; i < 4; ++i)
        if (kQuadViews[i] == v) return i;
    return -1;
}

void enterQuad(OrbitalCamera& camera, ViewportState& vp) {
    if (vp.quad) return;
    const int cur = quadPanelOf(camera.view());
    for (int i = 0; i < 4; ++i) {
        vp.quadCams[i] = camera;  // mismo objetivo y distancia
        if (i != cur) vp.quadCams[i].setView(kQuadViews[i]);
    }
    vp.quadActive = cur >= 0 ? cur : 0;
    camera = vp.quadCams[vp.quadActive];
    vp.quad = true;
}

// Sale de la vista de 4 maximizando el panel de `v` (o la activa cambiada a `v`).
void exitQuad(OrbitalCamera& camera, ViewportState& vp, CamView v) {
    vp.quadCams[vp.quadActive] = camera;
    const int p = quadPanelOf(v);
    if (p >= 0) camera = vp.quadCams[p];
    else camera.setView(v);
    vp.quad = false;
}

void selectView(OrbitalCamera& camera, CamView v) {
    if (g_vp && g_vp->quad) exitQuad(camera, *g_vp, v);
    else camera.setView(v);
}

void toggleQuad(OrbitalCamera& camera) {
    if (!g_vp) return;
    if (g_vp->quad) exitQuad(camera, *g_vp, camera.view());
    else enterQuad(camera, *g_vp);
}

const char* viewLabel(CamView v, const char* perspName) {
    switch (v) {
        case CamView::Top: return "Superior";
        case CamView::Right: return "Derecha";
        case CamView::Front: return "Frontal";
        case CamView::Left: return "Izquierda";
        case CamView::Back: return "Trasera";
        case CamView::Bottom: return "Inferior";
        default: return perspName;
    }
}

// --- Utilidades -----------------------------------------------------------------

const char* defaultNameFor(PrimitiveType type) {
    switch (type) {
        case PrimitiveType::Cube: return "Cubo";
        case PrimitiveType::Sphere: return "Esfera";
        case PrimitiveType::Plane: return "Plano";
        case PrimitiveType::Light: return "Luz";
        case PrimitiveType::Camera: return "Cámara";
        case PrimitiveType::Mesh: return "Polígono";
        case PrimitiveType::None: default: return "Nulo";
    }
}

Icon iconFor(PrimitiveType type) {
    switch (type) {
        case PrimitiveType::Cube: return Icon::Cube;
        case PrimitiveType::Sphere: return Icon::Sphere;
        case PrimitiveType::Plane: return Icon::Plane;
        case PrimitiveType::Light: return Icon::Light;
        case PrimitiveType::Camera: return Icon::Camera;
        case PrimitiveType::Mesh: return Icon::Polygon;
        case PrimitiveType::None: default: return Icon::Null;
    }
}

bool hasGeometry(PrimitiveType t) {
    return t == PrimitiveType::Cube || t == PrimitiveType::Sphere || t == PrimitiveType::Plane ||
           t == PrimitiveType::Mesh;
}

// Medida base de cada primitiva en cm (antes de escalar), para el modo
// "Tamano" del Gestor de Coordenadas.
glm::vec3 baseSizeCm(PrimitiveType t) {
    switch (t) {
        case PrimitiveType::Cube:
        case PrimitiveType::Sphere: return glm::vec3(kCmPerUnit);
        case PrimitiveType::Plane: return glm::vec3(kCmPerUnit, 0.0f, kCmPerUnit);
        default: return glm::vec3(0.0f);
    }
}

// Igual, pero un Objeto poligonal mide lo que mida su malla.
glm::vec3 baseSizeCm(const SceneNode& n) {
    if (n.primitive == PrimitiveType::Mesh && n.mesh) return (n.mesh->boundsMax - n.mesh->boundsMin) * kCmPerUnit;
    return baseSizeCm(n.primitive);
}

void setHint(const char* text) {
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) g.hint = text;
}

void fixupMaterialIndicesAfterRemoval(SceneNode& node, int removed) {
    if (node.materialIndex == removed) node.materialIndex = -1;
    else if (node.materialIndex > removed) node.materialIndex -= 1;
    for (const auto& child : node.children()) fixupMaterialIndicesAfterRemoval(*child, removed);
}

bool materialUsed(const SceneNode& node, int index) {
    if (node.materialIndex == index) return true;
    for (const auto& c : node.children())
        if (materialUsed(*c, index)) return true;
    return false;
}

void collectUsers(SceneNode& node, int index, std::vector<SceneNode*>& out) {
    if (node.materialIndex == index) out.push_back(&node);
    for (const auto& c : node.children()) collectUsers(*c, index, out);
}

// --- Seleccion multiple -------------------------------------------------------
// Recoge los nodos seleccionados (los del conjunto g.selIds) en orden de arbol.
void collectSelected(SceneNode& node, std::vector<SceneNode*>& out) {
    for (const auto& c : node.children()) {
        if (g.selIds.count(c->id())) out.push_back(c.get());
        collectSelected(*c, out);
    }
}

// Primer nodo de la seleccion (nuevo objeto activo al quitar el anterior).
SceneNode* firstSelected(SceneNode& root) {
    std::vector<SceneNode*> v;
    collectSelected(root, v);
    return v.empty() ? nullptr : v.front();
}

SceneNode* findById(SceneNode& node, int id) {
    if (node.id() == id) return &node;
    for (const auto& c : node.children())
        if (SceneNode* f = findById(*c, id)) return f;
    return nullptr;
}

// Esfera envolvente aproximada (mundo) de un nodo y sus hijos, para Encuadrar.
void accumulateBounds(const SceneNode& node, glm::vec3& mn, glm::vec3& mx, bool& any) {
    if (node.primitive != PrimitiveType::None) {
        const glm::mat4 w = node.worldMatrix();
        glm::vec3 lo(-0.5f), hi(0.5f);
        if (node.primitive == PrimitiveType::Mesh && node.mesh) {
            lo = node.mesh->boundsMin;
            hi = node.mesh->boundsMax;
        }
        for (int i = 0; i < 8; ++i) {
            const glm::vec3 corner((i & 1) ? hi.x : lo.x, (i & 2) ? hi.y : lo.y, (i & 4) ? hi.z : lo.z);
            const glm::vec3 p = glm::vec3(w * glm::vec4(corner, 1.0f));
            mn = any ? glm::min(mn, p) : p;
            mx = any ? glm::max(mx, p) : p;
            any = true;
        }
    }
    for (const auto& c : node.children()) accumulateBounds(*c, mn, mx, any);
}

void frameNodes(OrbitalCamera& camera, const SceneNode& node, bool includeSelf) {
    glm::vec3 mn(0.0f), mx(0.0f);
    bool any = false;
    if (includeSelf) accumulateBounds(node, mn, mx, any);
    else
        for (const auto& c : node.children()) accumulateBounds(*c, mn, mx, any);
    if (!any) {
        camera.resetView();
        return;
    }
    camera.frame((mn + mx) * 0.5f, std::max(0.25f, glm::length(mx - mn) * 0.5f));
}

// --- Widgets propios -----------------------------------------------------------

enum class BtnResult { None, Click, Palette };

// Tooltip al estilo C4D: primera linea = titulo (blanco), el resto =
// descripcion y atajo en gris claro.
void richTooltip(const char* text) {
    const char* nl = std::strchr(text, '\n');
    ImGui::BeginTooltip();
    if (!nl) {
        ImGui::TextUnformatted(text);
    } else {
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 255, 255, 255));
        ImGui::TextUnformatted(text, nl);
        ImGui::PopStyleColor();
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(190, 190, 190, 255));
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 26.0f);
        ImGui::TextUnformatted(nl + 1);
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
    }
    ImGui::EndTooltip();
}

// Boton de icono estilo barra de C4D. Con `palette`, mantener pulsado (o clic
// derecho) despliega la paleta del grupo en vez de crear el objeto por defecto,
// igual que los iconos con triangulito de C4D.
BtnResult iconButton(const char* id, Icon icon, float size, bool active, bool enabled, const char* tooltip,
                     const char* hint, bool palette = false) {
    ImGui::PushID(id);
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##btn", ImVec2(size, size));
    const bool hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled);
    const bool held = ImGui::IsItemActive();
    BtnResult result = BtnResult::None;
    static bool paletteFired = false;
    if (enabled) {
        if (palette && held && ImGui::GetIO().MouseDownDuration[0] > 0.35f && !paletteFired) {
            paletteFired = true;
            result = BtnResult::Palette;
        }
        if (palette && hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) result = BtnResult::Palette;
        if (ImGui::IsItemDeactivated()) {
            if (hovered && !paletteFired) result = BtnResult::Click;
            paletteFired = false;
        }
    }

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 max(pos.x + size, pos.y + size);
    if (active) {
        dl->AddRectFilled(pos, max, kToolBlue, 2.0f);
        dl->AddRect(pos, max, kAmber, 2.0f, 0, 1.0f);
    } else if (enabled && held) {
        dl->AddRectFilled(pos, max, IM_COL32(0, 0, 0, 60), 2.0f);
    } else if (enabled && hovered) {
        dl->AddRectFilled(pos, max, IM_COL32(255, 255, 255, 24), 2.0f);
    }
    const float pad = size * 0.14f;
    icons::draw(dl, icon, ImVec2(pos.x + pad, pos.y + pad), size - 2 * pad, enabled);
    if (palette) {
        dl->AddTriangleFilled(ImVec2(max.x - 2, max.y - 7), ImVec2(max.x - 2, max.y - 2), ImVec2(max.x - 7, max.y - 2),
                              enabled ? IM_COL32(220, 220, 220, 255) : IM_COL32(120, 120, 120, 255));
    }
    if (hovered && tooltip) richTooltip(tooltip);
    if (hovered && hint) g.hint = hint;
    ImGui::PopID();
    return result;
}

void toolbarSeparator(bool vertical, float extent) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    if (vertical) {
        dl->AddLine(ImVec2(p.x + 3, p.y + 3), ImVec2(p.x + 3, p.y + extent - 3), kBorderDark);
        dl->AddLine(ImVec2(p.x + 4, p.y + 3), ImVec2(p.x + 4, p.y + extent - 3), kBorderLight);
        ImGui::Dummy(ImVec2(8, extent));
    } else {
        dl->AddLine(ImVec2(p.x + 3, p.y + 3), ImVec2(p.x + extent - 3, p.y + 3), kBorderDark);
        dl->AddLine(ImVec2(p.x + 3, p.y + 4), ImVec2(p.x + extent - 3, p.y + 4), kBorderLight);
        ImGui::Dummy(ImVec2(extent, 8));
    }
}

// Asa de arrastre de 6 puntos en la esquina de cada panel (seccion 12).
// Con un preset que no es el de C4D, a la derecha del asa va una "ficha" con
// el nombre del editor (como el selector de editor de Blender o la pestana de
// cada panel en Maya/Max/SFM), porque esos presets no llevan pestanas
// verticales que digan que panel es cada uno.
void panelGrip(const char* chip = nullptr) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float h = ImGui::GetFrameHeight();
    for (int y = 0; y < 3; ++y)
        for (int x = 0; x < 2; ++x)
            dl->AddRectFilled(ImVec2(p.x + 2 + x * 4, p.y + h * 0.5f - 5 + y * 4),
                              ImVec2(p.x + 4 + x * 4, p.y + h * 0.5f - 3 + y * 4), kGrip);
    ImGui::Dummy(ImVec2(12, h * 0.6f));
    if (!chip || isC4DArrangement()) return;
    ImGui::SameLine(0, 2);
    const ImVec2 c = ImGui::GetCursorScreenPos();
    const ImVec2 ts = ImGui::CalcTextSize(chip);
    const float ch = h - 4.0f;
    const ImVec2 a(c.x, p.y + (h - ch) * 0.5f), b(c.x + ts.x + 14.0f, a.y + ch);
    dl->AddRectFilled(a, b, kTabOn, ImGui::GetStyle().FrameRounding);
    dl->AddRect(a, b, kBorderDark, ImGui::GetStyle().FrameRounding);
    dl->AddText(ImVec2(a.x + 7, a.y + (ch - ts.y) * 0.5f), kTextCol, chip);
    ImGui::Dummy(ImVec2(b.x - a.x + 4, h * 0.6f));
}

// Hueco reservado a la izquierda de cada entrada de menu para el icono
// (MenuItemEx lo mide con CalcTextSize, por eso son espacios).
constexpr const char* kIconGutterText = "      ";

struct MenuIconSlot {
    ImDrawList* dl;
    ImVec2 pos;
    float lh;
    MenuIconSlot() : dl(ImGui::GetWindowDrawList()), pos(ImGui::GetCursorScreenPos()), lh(ImGui::GetTextLineHeight()) {}
    void draw(Icon icon, bool enabled) const {
        const float s = std::min(16.0f, lh + 1.0f);
        icons::draw(dl, icon, ImVec2(pos.x + 1, pos.y + (lh - s) * 0.5f), s, enabled);
    }
};

// MenuItem con icono propio en la franja izquierda, como los menus de C4D R19.
bool iconMenuItem(Icon icon, const char* label, const char* shortcut = nullptr, bool selected = false,
                  bool enabled = true) {
    const MenuIconSlot slot;
    const bool r = ImGui::MenuItemEx(label, kIconGutterText, shortcut, selected, enabled);
    slot.draw(icon, enabled);
    return r;
}
bool iconMenuItem(Icon icon, const char* label, const char* shortcut, bool* p_selected, bool enabled = true) {
    const MenuIconSlot slot;
    const bool r = ImGui::MenuItemEx(label, kIconGutterText, shortcut, p_selected ? *p_selected : false, enabled);
    if (r && p_selected) *p_selected = !*p_selected;
    slot.draw(icon, enabled);
    return r;
}

// Entrada de menu con la rueda dentada de opciones a la derecha (C4D R19):
// clic en el texto ejecuta la orden; clic en la rueda pone `*gear` a true y
// devuelve false (quien llama abre las opciones). `icon` puede ser nullptr.
bool gearMenuItem(const Icon* icon, const char* label, const char* shortcut, bool selected, bool enabled, bool* gear) {
    const MenuIconSlot slot;
    const std::string sc = std::string(shortcut ? shortcut : "") + "      ";  // hueco para la rueda
    const bool r = ImGui::MenuItemEx(label, kIconGutterText, sc.c_str(), selected, enabled);
    if (icon) slot.draw(*icon, enabled);
    const ImVec2 mn = ImGui::GetItemRectMin(), mx = ImGui::GetItemRectMax();
    const float sz = std::min(14.0f, mx.y - mn.y - 2.0f);
    const ImVec2 gp(mx.x - sz - 4.0f, mn.y + (mx.y - mn.y - sz) * 0.5f);
    const bool overGear = ImGui::IsItemHovered() && ImGui::GetIO().MousePos.x >= gp.x - 3.0f;
    if (overGear) ImGui::GetWindowDrawList()->AddRect(ImVec2(gp.x - 2, gp.y - 2), ImVec2(gp.x + sz + 2, gp.y + sz + 2),
                                                      IM_COL32(200, 200, 200, 160), 2.0f);
    icons::draw(ImGui::GetWindowDrawList(), Icon::Gear, gp, sz, enabled);
    if (r && overGear) {
        if (gear) *gear = true;
        return false;
    }
    return r;
}

// Submenu con icono. Si se abre, la ventana actual pasa a ser el popup, asi
// que el icono se pinta en la lista de dibujo de la ventana padre, guardada
// antes de la llamada (encima del resaltado de la cabecera).
bool iconBeginMenu(Icon icon, const char* label, bool enabled = true) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float lh = ImGui::GetTextLineHeight();
    const bool open = ImGui::BeginMenuEx(label, kIconGutterText, enabled);
    const float s = std::min(16.0f, lh + 1.0f);
    icons::draw(dl, icon, ImVec2(pos.x + 1, pos.y + (lh - s) * 0.5f), s, enabled);
    return open;
}

constexpr ImGuiWindowFlags kPanelFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove |
                                         ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
                                         ImGuiWindowFlags_NoSavedSettings |
                                         ImGuiWindowFlags_NoBringToFrontOnFocus;

bool beginPanel(const char* name, ImVec2 pos, ImVec2 size, ImGuiWindowFlags extra = 0) {
    ImGui::SetNextWindowPos(pos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(size, ImGuiCond_Always);
    return ImGui::Begin(name, nullptr, kPanelFlags | extra);
}

// icons::materialBall sigue tomando specular/shininess (preview 2D barato,
// sin relacion con el shader PBR de verdad) -- aqui se aproxima a partir de
// metallic/roughness solo para que la bolita de preview no se quede
// desincronizada del material real tras el cambio a PBR (2026-10-30, ver
// ROADMAP_RENDER.md). Mismo criterio que el fallback de OpenGLRenderer.
void materialBallSpecShininess(const Material& m, float& outSpecular, float& outShininess) {
    outSpecular = 0.2f + 0.8f * m.metallic;
    outShininess = 96.0f - 92.0f * m.roughness;
}

// Fila de propiedad del Gestor de Atributos: circulo de animacion + etiqueta
// con puntos guia hasta la columna de valores ("Nombre . . . . . [   ]").
float g_labelW = 150.0f;
void propLabel(const char* label, bool animatable = true) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float h = ImGui::GetFrameHeight();
    if (animatable) dl->AddCircle(ImVec2(p.x + 6, p.y + h * 0.5f), 3.5f, IM_COL32(140, 140, 140, 255), 12, 1.0f);
    const ImVec2 ts = ImGui::CalcTextSize(label);
    const float x = p.x + 14;
    // Recorta la etiqueta a su columna (en dos columnas no debe pisar el campo).
    dl->PushClipRect(p, ImVec2(p.x + g_labelW - 4.0f, p.y + h), true);
    dl->AddText(ImVec2(x, p.y + (h - ts.y) * 0.5f), kTextCol, label);
    dl->PopClipRect();
    if (ts.x + 18.0f > g_labelW && ImGui::IsMouseHoveringRect(p, ImVec2(p.x + g_labelW, p.y + h)))
        ImGui::SetTooltip("%s", label);
    for (float d = x + ts.x + 5.0f; d < p.x + g_labelW - 6.0f; d += 4.0f)
        dl->AddRectFilled(ImVec2(d, p.y + h * 0.5f + 3), ImVec2(d + 1, p.y + h * 0.5f + 4), IM_COL32(110, 110, 110, 255));
    ImGui::Dummy(ImVec2(g_labelW, h));
    ImGui::SameLine(0, 0);
}

float fieldWidth(float preferred = 170.0f) { return std::min(preferred, ImGui::GetContentRegionAvail().x - 4.0f); }

void sectionHeader(const char* title) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float w = ImGui::GetContentRegionAvail().x;
    const float h = ImGui::GetTextLineHeight() + 6;
    dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), kSectionBg);
    dl->AddLine(ImVec2(p.x, p.y + h), ImVec2(p.x + w, p.y + h), kBorderDark);
    if (g_fontBold) ImGui::PushFont(g_fontBold);
    dl->AddText(ImVec2(p.x + 6, p.y + 3), kTextCol, title);
    if (g_fontBold) ImGui::PopFont();
    ImGui::Dummy(ImVec2(w, h + 2));
}

void roFloat(const char* label, float value, const char* fmt, bool animatable = true) {
    propLabel(label, animatable);
    ImGui::BeginDisabled();
    ImGui::SetNextItemWidth(fieldWidth());
    ImGui::PushID(label);
    ImGui::DragFloat("##v", &value, 1.0f, 0.0f, 0.0f, fmt);
    ImGui::PopID();
    ImGui::EndDisabled();
    setHint("Parámetro todavía no implementado en Iron3D");
}

void roCombo(const char* label, const char* value, bool animatable = false) {
    propLabel(label, animatable);
    ImGui::BeginDisabled();
    ImGui::SetNextItemWidth(fieldWidth());
    ImGui::PushID(label);
    if (ImGui::BeginCombo("##v", value)) ImGui::EndCombo();
    ImGui::PopID();
    ImGui::EndDisabled();
    setHint("Parámetro todavía no implementado en Iron3D");
}

void roCheck(const char* label, bool value, bool animatable = true) {
    propLabel(label, animatable);
    ImGui::BeginDisabled();
    ImGui::PushID(label);
    ImGui::Checkbox("##v", &value);
    ImGui::PopID();
    ImGui::EndDisabled();
    setHint("Parámetro todavía no implementado en Iron3D");
}

// Fila de "pestanas" del Gestor de Atributos: en C4D son botones conmutables,
// clic = solo esa, Mayus+clic = anade/quita (se ven varias a la vez).
void attrTabRow(const char* const* names, int count, unsigned& mask) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float avail = ImGui::GetContentRegionAvail().x;
    float x = 0.0f;
    for (int i = 0; i < count; ++i) {
        const ImVec2 ts = ImGui::CalcTextSize(names[i]);
        const float w = ts.x + 14.0f;
        if (i > 0) {
            if (x + w > avail) x = 0.0f;
            else ImGui::SameLine(0, 2);
        }
        const ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::PushID(i);
        if (ImGui::InvisibleButton("##tab", ImVec2(w, ts.y + 6))) {
            if (ImGui::GetIO().KeyShift) {
                mask ^= (1u << i);
                if (mask == 0) mask = 1u << i;
            } else {
                mask = 1u << i;
            }
        }
        const bool on = (mask >> i) & 1u;
        const bool hov = ImGui::IsItemHovered();
        const ImVec2 q(p.x + w, p.y + ts.y + 6);
        dl->AddRectFilled(p, q, on ? kTabActive : (hov ? IM_COL32(78, 78, 78, 255) : IM_COL32(64, 64, 64, 255)), 2.0f);
        dl->AddText(ImVec2(p.x + 7, p.y + 3), on ? IM_COL32(0, 0, 0, 255) : kTextCol, names[i]);
        if (hov) g.hint = "Clic: mostrar esta página. Mayús+Clic: mostrar varias a la vez.";
        ImGui::PopID();
        x += w + 2;
    }
    ImGui::Dummy(ImVec2(1, 2));
}

bool tabOn(unsigned mask, int i) { return (mask >> i) & 1u; }

// --- Undo para widgets editados durante varios frames ------------------------
// Snapshot al activarse el widget (antes de que el cambio se aplique) y
// commit al soltarlo: un solo paso de deshacer por edicion, nunca uno por frame.
std::string g_pendingSnap;
void trackEdit(SceneHistory& history, const SceneNode& root) {
    if (ImGui::IsItemActivated()) g_pendingSnap = history.snapshot(root);
    if (ImGui::IsItemDeactivatedAfterEdit()) history.commit(g_pendingSnap);
}

// --- Creacion de objetos -----------------------------------------------------------

// Seccion 18.6: Alt+clic crea el objeto como hijo del seleccionado; un clic
// normal lo crea en la raiz de la escena. Siempre queda seleccionado.
SceneNode* createObject(SceneNode& root, SceneNode*& selected, SceneHistory& history, PrimitiveType type) {
    SceneNode* parent = &root;
    if (ImGui::GetIO().KeyAlt && selected) parent = selected;
    history.commitBefore(root);
    selected = parent->addChild(defaultNameFor(type), type);    g.focus = AttrFocus::Object;
    if (parent != &root) g.collapsed.erase(parent->id());
    return selected;
}

void newScene(SceneNode& root, SceneNode*& selected, SceneHistory& history, std::vector<Material>& materials) {
    history.commitBefore(root); // Ctrl+Z recupera la escena anterior
    root.clearChildren();
    materials.clear();
    selected = nullptr;
    g.selectedMat = -1;
    g.focus = AttrFocus::Object;
    g.frame = (float)g.minFrame;
}

void offsetMaterialIndices(SceneNode& node, int base) {
    if (node.materialIndex >= 0) node.materialIndex += base;
    for (const auto& c : node.children()) offsetMaterialIndices(*c, base);
}

void reportFileError(const std::filesystem::path& path, const std::string& err) {
    g.importReport = C4DImportReport{};
    g.importReport.fileName = pathUtf8(path.filename());
    g.importError = err;
    g.showImport = true;
}

void frameNodes(OrbitalCamera& camera, const SceneNode& node, bool includeSelf);

// Como reportFileError, con un nombre ya en UTF-8 (elementos de una libreria).
void reportNamedError(const std::string& name, const std::string& err) {
    g.importReport = C4DImportReport{};
    g.importReport.fileName = name;
    g.importError = err;
    g.showImport = true;
}

// Monta una .lib4d para esta sesion y abre el Navegador de Contenido en ella.
void openPresetLibrary(const std::filesystem::path& path) {
    std::string err;
    if (!mountTemporaryLibrary(path, &err)) return reportFileError(path, err);
    g.browserOpen = true;
}

// Archivo > Abrir Libreria de Preestablecidos... del propio navegador.
void openPresetLibraryDialog() {
    if (!nativeFileDialogAvailable()) {
        reportFileError("Abrir librería", "Abrir una librería necesita el diálogo de archivos del sistema, que en "
                                          "esta plataforma todavía no existe. Cópiala a library/browser.");
        return;
    }
    const auto path = openFileDialog("Abrir librería de preestablecidos",
                                     {{"Librerías de preestablecidos de Cinema 4D", "*.lib4d"}});
    if (path) openPresetLibrary(*path);
}

// Texturas de un preestablecido: viven dentro de la libreria (tex/...). Las
// que usan los materiales recien importados y no estan en disco se sacan a
// `dir`/tex, y los materiales pasan a buscarlas ahi.
void extractPresetTextures(const PresetLibrary& lib, const std::filesystem::path& dir, std::vector<Material>& materials,
                           size_t firstNew) {
    auto lower = [](std::string t) {
        for (char& c : t) c = (char)std::tolower((unsigned char)c);
        return t;
    };
    std::map<std::string, const PresetItem*> byLeaf;
    for (const PresetItem& it : lib.items())
        if (it.kind == PresetKind::File) byLeaf.emplace(lower(it.name), &it);
    const std::string dirUtf8 = pathUtf8(dir);
    std::error_code ec;
    for (size_t i = firstNew; i < materials.size(); ++i) {
        Material& m = materials[i];
        std::vector<std::string> paths(std::begin(m.textures), std::end(m.textures));
        for (const ReflectLayer& l : m.layers) paths.insert(paths.end(), {l.roughnessTex, l.colorTex});
        for (const std::string& t : paths) {
            if (t.empty() || !resolveTexturePath(t, m.texBaseDir).empty()) continue;
            std::string leaf = t;
            std::replace(leaf.begin(), leaf.end(), '\\', '/');
            leaf = leaf.substr(leaf.rfind('/') + 1);
            auto it = byLeaf.find(lower(leaf));
            if (it == byLeaf.end()) continue;
            const auto out = dir / "tex" / std::filesystem::path(std::u8string(it->second->name.begin(), it->second->name.end()));
            if (!std::filesystem::exists(out, ec)) {
                std::vector<uint8_t> bytes;
                if (!lib.readData(*it->second, bytes)) continue;
                std::filesystem::create_directories(out.parent_path(), ec);
                std::ofstream f(out, std::ios::binary | std::ios::trunc);
                f.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
            }
        }
        m.texBaseDir = dirUtf8;
    }
}

// Carga un modelo de una libreria .lib4d en la escena (doble clic en el
// navegador o arrastrarlo al visor), como en C4D: se fusiona con lo que haya.
// El modelo es un .c4d completo dentro de la libreria: se descomprime a un
// archivo temporal y pasa por el importador de .c4d de siempre. Se puede deshacer.
void loadPresetImpl(const PresetLibrary& lib, const PresetItem& item, SceneNode& root, SceneNode*& selected,
                         SceneHistory& history, std::vector<Material>& materials) {
    std::vector<uint8_t> data;
    std::string err;
    if (!lib.readData(item, data, &err)) return reportNamedError(item.name, err);
    if (data.size() < 8 || std::memcmp(data.data(), "QC4D", 4) != 0)
        return reportNamedError(item.name, "Este preestablecido no es un modelo de Cinema 4D.");
    std::error_code ec;
    const auto dir = std::filesystem::temp_directory_path(ec) / "Iron3D";
    std::filesystem::create_directories(dir, ec);
    const auto tmp = dir / ("preset_" + std::to_string(item.id) + ".c4d");
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        f.write(reinterpret_cast<const char*>(data.data()), std::streamsize(data.size()));
        if (!f) return reportNamedError(item.name, "No se pudo escribir el archivo temporal en " + pathUtf8(dir) + ".");
    }
    data.clear();
    data.shrink_to_fit();
    const std::string before = history.snapshot(root);
    C4DImportReport rep;
    const size_t firstNewMaterial = materials.size();
    const bool ok = importC4D(tmp, root, materials, rep, &err);
    std::filesystem::remove(tmp, ec);
    if (!ok) return reportNamedError(item.name, err);
    extractPresetTextures(lib, dir, materials, firstNewMaterial);
    history.commit(before);
    selected = nullptr;
    noteRecentPreset(lib, item);
    // Como en C4D, cargar un preestablecido no abre ninguna ventana... salvo
    // que algo no haya llegado tal cual: entonces sale el informe.
    rep.fileName = item.name + " (" + lib.title() + ")";
    if (!rep.warnings.empty()) {
        g.importReport = rep;
        g.importError.clear();
        g.showImport = true;
    }
}

// Archivo > Abrir (Ctrl+O): .iron o .c4d con el dialogo del sistema. Sin
// dialogo nativo (Linux por ahora) se usa el propio de la carpeta scenes/.
void openSceneFile(SceneNode& root, SceneNode*& selected, SceneHistory& history, std::vector<Material>& materials,
                   OrbitalCamera& camera) {
    if (!nativeFileDialogAvailable()) {
        g.showScene = true;
        g.sceneLoadMode = true;
        return;
    }
    const auto path = openFileDialog("Abrir", {{"Escenas y librerías (iron, c4d, lib4d)", "*.iron;*.c4d;*.lib4d"},
                                               {"Iron3D", "*.iron"}, {"Cinema 4D", "*.c4d"},
                                               {"Librería de preestablecidos de Cinema 4D", "*.lib4d"}});
    if (!path) return;
    // Una .lib4d no sustituye la escena: se monta en el Navegador de Contenido
    // (Preestablecidos, solo esta sesion) y se abre su ventana, como en C4D.
    if (isLib4dPath(*path)) return openPresetLibrary(*path);
    std::string err;
    if (isC4DPath(*path)) {
        SceneNode temp("tmp");
        std::vector<Material> mats;
        C4DImportReport rep;
        if (!importC4D(*path, temp, mats, rep, &err)) return reportFileError(*path, err);
        root.clearChildren();
        root.adoptChildrenFrom(temp);
        materials = std::move(mats);
        g.importReport = rep;
        g.importError.clear();
        g.showImport = true;
    } else if (!loadScene(*path, root, materials, &err)) {
        return reportFileError(*path, err);
    }
    selected = nullptr;
    history.reset();
    g.selectedMat = -1;
    frameNodes(camera, root, false);
}

// Archivo > Fusionar (Ctrl+Mayus+O): anade a la escena actual lo que haya en
// otro .c4d o .iron, como en C4D. Se puede deshacer.
void mergeSceneFile(SceneNode& root, SceneNode*& selected, SceneHistory& history, std::vector<Material>& materials) {
    if (!nativeFileDialogAvailable()) {
        reportFileError("Fusionar", "Fusionar necesita el diálogo de archivos del sistema, que en esta "
                                    "plataforma todavía no existe.");
        return;
    }
    const auto path = openFileDialog("Fusionar", {{"Escenas de Cinema 4D e Iron3D", "*.c4d;*.iron"},
                                                  {"Cinema 4D", "*.c4d"}, {"Iron3D", "*.iron"}});
    if (!path) return;
    const std::string before = history.snapshot(root);
    std::string err;
    if (isC4DPath(*path)) {
        C4DImportReport rep;
        if (!importC4D(*path, root, materials, rep, &err)) return reportFileError(*path, err);
        g.importReport = rep;
        g.importError.clear();
        g.showImport = true;
    } else {
        SceneNode temp("tmp");
        std::vector<Material> mats;
        if (!loadScene(*path, temp, mats, &err)) return reportFileError(*path, err);
        offsetMaterialIndices(temp, (int)materials.size());
        for (Material& m : mats) materials.push_back(std::move(m));
        root.appendChildrenFrom(temp);
    }
    history.commit(before);
    selected = nullptr;
}

// Seleccion sin repetir: si un padre esta seleccionado, sus hijos ya van con el.
std::vector<const SceneNode*> topSelected(SceneNode& root, SceneNode* active) {
    std::vector<SceneNode*> all;
    collectSelected(root, all);
    if (all.empty() && active) all.push_back(active);
    std::vector<const SceneNode*> out;
    for (SceneNode* n : all) {
        bool underSelected = false;
        for (SceneNode* p = n->parent(); p && !underSelected; p = p->parent()) underSelected = g.selIds.count(p->id()) > 0;
        if (!underSelected) out.push_back(n);
    }
    return out;
}

// Archivo > Exportar > Cinema 4D (toda la escena) y Guardar Objetos
// Seleccionados como... (solo la seleccion), con el exportador de Cineware.
void exportC4DDialog(const std::vector<const SceneNode*>& roots, const std::vector<Material>& materials,
                     bool allMaterials, const std::string& title, const std::string& defaultName) {
    if (roots.empty()) return;
    if (!nativeFileDialogAvailable()) {
        reportNamedError(title, "Exportar necesita el diálogo de archivos del sistema, que en esta plataforma "
                                "todavía no existe.");
        return;
    }
    const auto path = saveFileDialog(title, {{"Cinema 4D", "*.c4d"}}, defaultName, "c4d");
    if (!path) return;
    std::string err;
    C4DExportReport rep;
    if (!exportC4D(*path, roots, materials, allMaterials, rep, &err, g.fps)) return reportFileError(*path, err);
    g.exportReport = rep;
    g.exportPath = pathUtf8(*path);
    g.showExport = true;
}

void exportSceneC4D(SceneNode& root, const std::vector<Material>& materials) {
    std::vector<const SceneNode*> roots;
    for (const auto& c : root.children()) roots.push_back(c.get());
    if (roots.empty()) return reportNamedError("Exportar", "La escena está vacía.");
    exportC4DDialog(roots, materials, true, "Exportar a Cinema 4D", "Escena.c4d");
}

void exportSelectedC4D(SceneNode& root, SceneNode* selected, const std::vector<Material>& materials) {
    const auto roots = topSelected(root, selected);
    if (roots.empty()) return;
    exportC4DDialog(roots, materials, false, "Guardar Objetos Seleccionados como",
                    roots.size() == 1 ? roots.front()->name() + ".c4d" : std::string("Objetos.c4d"));
}

// Seleccion sin los nodos que ya cuelgan de otro seleccionado (al mover o
// borrar un padre, sus hijos van con el). Si `clicked` no esta en la
// seleccion (clic derecho sobre otra fila), la operacion es solo sobre el.
std::vector<SceneNode*> topLevelSelection(SceneNode& root, SceneNode* clicked) {
    if (clicked && !g.selIds.count(clicked->id())) return {clicked};
    std::vector<SceneNode*> all, out;
    collectSelected(root, all);
    for (SceneNode* n : all) {
        bool under = false;
        for (SceneNode* p = n->parent(); p && !under; p = p->parent()) under = g.selIds.count(p->id()) > 0;
        if (!under) out.push_back(n);
    }
    if (out.empty() && clicked) out.push_back(clicked);
    return out;
}

void deleteSelected(SceneNode& root, SceneNode*& selected, SceneHistory& history) {
    if (!selected || !selected->parent()) return;
    history.commitBefore(root);
    // Borra toda la seleccion, no solo el objeto activo. Solo los de arriba:
    // borrar un padre ya destruye a sus hijos seleccionados.
    std::vector<SceneNode*> doomed = topLevelSelection(root, nullptr);
    if (doomed.empty()) doomed.push_back(selected);
    for (SceneNode* n : doomed)
        if (n->parent()) n->parent()->removeChild(n);
    g.selIds.clear();
    selected = nullptr;
}

// Mueve `n` bajo `newParent` en la posicion `index` sin que se mueva en mundo.
SceneNode* reparentKeepWorld(SceneNode* n, SceneNode& newParent, size_t index) {
    SceneNode* old = n->parent();
    if (!old) return n;
    const glm::mat4 world = n->worldMatrix();
    SceneNode* moved = newParent.insertChild(old->detachChild(n), index);
    moved->setLocalMatrix(glm::inverse(newParent.worldMatrix()) * world);
    return moved;
}

void selectOnly(SceneNode*& selected, SceneNode* n) {
    g.selIds.clear();
    if (n) g.selIds.insert(n->id());
    g.lastSelectedId = n ? n->id() : -1;
    selected = n;
}

// Agrupar Objetos (Alt+G): un Nulo nuevo en el sitio del primero, con la
// seleccion dentro y plegado, como en C4D.
void groupObjects(SceneNode& root, SceneNode*& selected, SceneHistory& history, SceneNode* clicked) {
    const std::vector<SceneNode*> nodes = topLevelSelection(root, clicked ? clicked : selected);
    if (nodes.empty() || !nodes.front()->parent()) return;
    history.commitBefore(root);
    SceneNode* parent = nodes.front()->parent();
    SceneNode* null = parent->insertChild(std::make_unique<SceneNode>("Nulo"), parent->indexOf(nodes.front()));
    for (SceneNode* n : nodes) reparentKeepWorld(n, *null, null->children().size());
    g.collapsed.insert(null->id());
    selectOnly(selected, null);
}

// Expandir Grupo de Objetos (Mayus+G) y Eliminar Sin Hijos: los hijos suben
// al sitio del objeto (sin moverse en mundo) y el objeto desaparece.
void dissolveNode(SceneNode& root, SceneNode*& selected, SceneHistory& history, SceneNode* n) {
    SceneNode* parent = n ? n->parent() : nullptr;
    if (!parent) return;
    history.commitBefore(root);
    size_t at = parent->indexOf(n);
    std::vector<SceneNode*> kids;
    for (const auto& c : n->children()) kids.push_back(c.get());
    g.selIds.clear();
    SceneNode* first = nullptr;
    for (SceneNode* k : kids) {
        SceneNode* moved = reparentKeepWorld(k, *parent, ++at);
        g.selIds.insert(moved->id());
        if (!first) first = moved;
    }
    parent->removeChild(n);
    selected = first;
    g.lastSelectedId = first ? first->id() : -1;
}

void forSubtree(SceneNode& n, const std::function<void(SceneNode&)>& fn) {
    fn(n);
    for (const auto& c : n.children()) forSubtree(*c, fn);
}

// --- Hacer Editable (C) ------------------------------------------------------------
// Un objeto parametrico (Cubo, Esfera, Plano) pasa a ser un Objeto Poligonal
// con la misma malla que se ve en el visor. Conserva nombre, hijos, material y
// transformacion, como en C4D. Actua sobre toda la seleccion (o sobre el
// objeto del clic derecho si no esta seleccionado).
bool nodeEditorVisible(const SceneNode* n);

bool isParametric(PrimitiveType t) {
    return t == PrimitiveType::Cube || t == PrimitiveType::Sphere || t == PrimitiveType::Plane;
}

std::vector<SceneNode*> editableTargets(SceneNode& root, SceneNode* selected, SceneNode* clicked) {
    std::vector<SceneNode*> all;
    if (clicked && !g.selIds.count(clicked->id())) all.push_back(clicked);
    else collectSelected(root, all);
    if (all.empty() && selected) all.push_back(selected);
    std::vector<SceneNode*> out;
    for (SceneNode* n : all)
        if (isParametric(n->primitive)) out.push_back(n);
    return out;
}

bool canMakeEditable(SceneNode& root, SceneNode* selected, SceneNode* clicked = nullptr) {
    return !editableTargets(root, selected, clicked).empty();
}

int makeEditable(SceneNode& root, SceneNode* selected, SceneHistory& history, SceneNode* clicked = nullptr) {
    const std::vector<SceneNode*> nodes = editableTargets(root, selected, clicked);
    if (nodes.empty()) return 0;
    history.commitBefore(root);
    for (SceneNode* n : nodes) {
        MeshData m = n->primitive == PrimitiveType::Cube     ? makeCubeMesh()
                     : n->primitive == PrimitiveType::Sphere ? makeSphereMesh()
                                                             : makePlaneMesh();
        updateBounds(m);  // los make*Mesh no la rellenan; sin ella fallan clic, Encuadrar y Tamaño
        n->mesh = std::make_shared<const MeshData>(std::move(m));
        n->primitive = PrimitiveType::Mesh;
    }
    return (int)nodes.size();
}

// Malla en el espacio local del nodo tal como se ve (parametricos incluidos).
bool nodeMeshData(const SceneNode& n, MeshData& out) {
    switch (n.primitive) {
        case PrimitiveType::Cube: out = makeCubeMesh(); break;
        case PrimitiveType::Sphere: out = makeSphereMesh(); break;
        case PrimitiveType::Plane: out = makePlaneMesh(); break;
        case PrimitiveType::Mesh:
            if (!n.mesh || n.mesh->indices.empty()) return false;
            out = *n.mesh;
            break;
        default: return false;
    }
    updateBounds(out);
    return true;
}

void selectAllObjects(SceneNode& root, SceneNode*& selected) {
    g.selIds.clear();
    SceneNode* first = nullptr;
    for (const auto& c : root.children())
        forSubtree(*c, [&](SceneNode& n) {
            g.selIds.insert(n.id());
            if (!first) first = &n;
        });
    if (!selected) selected = first;
    g.lastSelectedId = selected ? selected->id() : -1;
}

// Estado Actual a Objeto: copia poligonal al lado de cada seleccionado (con
// sus hijos, tambien convertidos); el original no se toca.
int currentStateToObject(SceneNode& root, SceneNode*& selected, SceneHistory& history) {
    const std::vector<SceneNode*> nodes = topLevelSelection(root, selected);
    bool any = false;
    for (SceneNode* n : nodes)
        forSubtree(*n, [&](SceneNode& s) { any = any || isParametric(s.primitive) || s.primitive == PrimitiveType::Mesh; });
    if (!any) return 0;
    history.commitBefore(root);
    int count = 0;
    SceneNode* last = nullptr;
    g.selIds.clear();
    for (SceneNode* n : nodes) {
        if (!n->parent()) continue;
        SceneNode* parent = n->parent();
        SceneNode* copy = duplicateSubtree(*parent, *n);
        copy = parent->insertChild(parent->detachChild(copy), parent->indexOf(n) + 1);  // justo debajo del original
        copy->setName(n->name());
        forSubtree(*copy, [&](SceneNode& s) {
            if (!isParametric(s.primitive)) return;
            MeshData m;
            if (!nodeMeshData(s, m)) return;
            s.mesh = std::make_shared<const MeshData>(std::move(m));
            s.primitive = PrimitiveType::Mesh;
        });
        g.selIds.insert(copy->id());
        last = copy;
        ++count;
    }
    if (last) {
        selected = last;
        g.lastSelectedId = last->id();
    }
    return count;
}

// Conectar Objetos: une la geometria de la seleccion (y sus hijos) en un solo
// Objeto Polígono, con el eje y el material del primero. Con `removeSources`
// (Conectar+Eliminar) los originales desaparecen.
// Soldar (opcion de Conectar Objetos): une los vertices que coinciden en
// posicion (dentro de `tol`), normal y color -- las costuras que deja
// conectar piezas que se tocan --, y quita los triangulos que quedan
// degenerados. Las aristas vivas no se tocan: ahi la normal es distinta.
int weldMesh(MeshData& m, float tol) {
    const float cell = std::max(tol, 1e-6f);
    auto key = [&](const glm::vec3& p) {
        return std::make_tuple((long long)std::floor(p.x / cell), (long long)std::floor(p.y / cell),
                               (long long)std::floor(p.z / cell));
    };
    std::map<std::tuple<long long, long long, long long>, std::vector<uint32_t>> grid;
    std::vector<Vertex> verts;
    std::vector<uint32_t> remap(m.vertices.size());
    for (size_t i = 0; i < m.vertices.size(); ++i) {
        const Vertex& v = m.vertices[i];
        const auto k = key(v.position);
        uint32_t found = UINT32_MAX;
        for (long long dx = -1; dx <= 1 && found == UINT32_MAX; ++dx)
            for (long long dy = -1; dy <= 1 && found == UINT32_MAX; ++dy)
                for (long long dz = -1; dz <= 1 && found == UINT32_MAX; ++dz) {
                    auto it = grid.find({std::get<0>(k) + dx, std::get<1>(k) + dy, std::get<2>(k) + dz});
                    if (it == grid.end()) continue;
                    for (uint32_t c : it->second) {
                        const Vertex& o = verts[c];
                        if (glm::length(o.position - v.position) <= tol && glm::dot(o.normal, v.normal) > 0.9995f &&
                            glm::length(o.color - v.color) < 1e-4f) {
                            found = c;
                            break;
                        }
                    }
                }
        if (found == UINT32_MAX) {
            found = (uint32_t)verts.size();
            verts.push_back(v);
            grid[k].push_back(found);
        }
        remap[i] = found;
    }
    std::vector<uint32_t> idx;
    idx.reserve(m.indices.size());
    const std::vector<uint32_t> faces = faceTriCounts(m);
    std::vector<uint32_t> newFaces;
    size_t tri = 0;
    for (uint32_t count : faces) {
        uint32_t kept = 0;
        for (uint32_t k = 0; k < count; ++k, ++tri) {
            const size_t i = tri * 3;
            const uint32_t a = remap[m.indices[i]], b = remap[m.indices[i + 1]], c = remap[m.indices[i + 2]];
            if (a == b || b == c || a == c) continue;
            idx.push_back(a);
            idx.push_back(b);
            idx.push_back(c);
            ++kept;
        }
        if (kept) newFaces.push_back(kept);
    }
    const int removed = (int)(m.vertices.size() - verts.size());
    m.vertices = std::move(verts);
    m.indices = std::move(idx);
    m.faceTris = std::move(newFaces);
    return removed;
}

int connectObjects(SceneNode& root, SceneNode*& selected, SceneHistory& history, bool removeSources) {
    const std::vector<SceneNode*> nodes = topLevelSelection(root, selected);
    struct Part { const SceneNode* n; glm::mat4 world; };
    std::vector<Part> parts;
    for (SceneNode* n : nodes)
        forSubtree(*n, [&](SceneNode& s) {
            if (hasGeometry(s.primitive) && nodeEditorVisible(&s)) parts.push_back({&s, s.worldMatrix()});
        });
    if (parts.empty() || !nodes.front()->parent()) return 0;
    SceneNode* first = nodes.front();
    SceneNode* parent = first->parent();
    const glm::mat4 newWorld = first->worldMatrix();
    const glm::mat4 toNew = glm::inverse(newWorld);

    MeshData out;
    for (const Part& p : parts) {
        MeshData m;
        if (!nodeMeshData(*p.n, m)) continue;
        const glm::mat4 xf = toNew * p.world;
        const glm::mat3 nxf = glm::transpose(glm::inverse(glm::mat3(xf)));
        const bool flip = glm::determinant(glm::mat3(xf)) < 0.0f;  // escala negativa: invertir el orden
        const uint32_t base = (uint32_t)out.vertices.size();
        for (Vertex v : m.vertices) {
            v.position = glm::vec3(xf * glm::vec4(v.position, 1.0f));
            const glm::vec3 nn = nxf * v.normal;
            v.normal = glm::length(nn) > 1e-8f ? glm::normalize(nn) : v.normal;
            out.vertices.push_back(v);
        }
        const std::vector<uint32_t> faces = faceTriCounts(m);
        out.faceTris.insert(out.faceTris.end(), faces.begin(), faces.end());
        for (size_t i = 0; i + 2 < m.indices.size(); i += 3) {
            out.indices.push_back(base + m.indices[i]);
            out.indices.push_back(base + (flip ? m.indices[i + 2] : m.indices[i + 1]));
            out.indices.push_back(base + (flip ? m.indices[i + 1] : m.indices[i + 2]));
        }
    }
    if (out.indices.empty()) return 0;
    if (g.connectWeld) weldMesh(out, std::max(0.0f, g.connectWeldCm) / kCmPerUnit);
    updateBounds(out);

    history.commitBefore(root);
    int material = -1;
    for (const Part& p : parts)
        if (p.n->materialIndex >= 0) { material = p.n->materialIndex; break; }
    const std::string name = first->name();
    auto made = std::make_unique<SceneNode>(name, PrimitiveType::Mesh);
    made->mesh = std::make_shared<const MeshData>(std::move(out));
    made->materialIndex = material;
    SceneNode* result = parent->insertChild(std::move(made), parent->indexOf(first) + 1);
    result->setLocalMatrix(glm::inverse(parent->worldMatrix()) * newWorld);
    if (removeSources)
        for (SceneNode* n : nodes)
            if (n->parent()) n->parent()->removeChild(n);
    selectOnly(selected, result);
    return (int)parts.size();
}

// Seleccion Rectangular (0) y Lazo (8): arrastrar en el visor dibuja la zona;
// al soltar se eligen los objetos que la toquen (rectangulo) o que tengan
// algun punto dentro (lazo). Mayus anade, Ctrl quita, sin nada sustituye.
bool pointInPolygon(const std::vector<ImVec2>& poly, ImVec2 p) {
    bool in = false;
    for (size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++) {
        const ImVec2 a = poly[i], b = poly[j];
        if ((a.y > p.y) != (b.y > p.y) && p.x < (b.x - a.x) * (p.y - a.y) / (b.y - a.y + 1e-12f) + a.x) in = !in;
    }
    return in;
}

void handleMarqueeSelection(SceneNode& root, SceneNode*& selected, const OrbitalCamera& camera, const ViewportState& vp) {
    ImGuiIO& io = ImGui::GetIO();
    if (!vp.selectTool || vp.selectShape == 0) {
        g.marquee = false;
        return;
    }
    const ImVec2 m = io.MousePos;
    const bool inVp = m.x >= vp.x && m.y >= vp.y && m.x < vp.x + vp.w && m.y < vp.y + vp.h;
    if (!g.marquee) {
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && inVp && !io.WantCaptureMouse && !io.KeyAlt) {
            g.marquee = true;
            g.marqueePts.assign(1, m);
        }
        return;
    }
    const ImVec2 clamped(std::clamp(m.x, vp.x, vp.x + vp.w), std::clamp(m.y, vp.y, vp.y + vp.h));
    if (vp.selectShape == 1) {
        if (g.marqueePts.size() < 2) g.marqueePts.push_back(clamped);
        else g.marqueePts[1] = clamped;
    } else {
        const ImVec2 l = g.marqueePts.back();
        if ((l.x - clamped.x) * (l.x - clamped.x) + (l.y - clamped.y) * (l.y - clamped.y) > 9.0f) g.marqueePts.push_back(clamped);
    }
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const ImU32 line = IM_COL32(255, 255, 255, 220), fill = IM_COL32(120, 160, 255, 40);
    if (vp.selectShape == 1 && g.marqueePts.size() == 2) {
        const ImVec2 a(std::min(g.marqueePts[0].x, g.marqueePts[1].x), std::min(g.marqueePts[0].y, g.marqueePts[1].y));
        const ImVec2 b(std::max(g.marqueePts[0].x, g.marqueePts[1].x), std::max(g.marqueePts[0].y, g.marqueePts[1].y));
        dl->AddRectFilled(a, b, fill);
        dl->AddRect(a, b, line);
    } else if (vp.selectShape == 2 && g.marqueePts.size() > 1) {
        dl->AddPolyline(g.marqueePts.data(), (int)g.marqueePts.size(), line, ImDrawFlags_None, 1.0f);
        dl->AddLine(g.marqueePts.back(), g.marqueePts.front(), IM_COL32(255, 255, 255, 110));
    }
    if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) return;

    // Al soltar: elegir.
    g.marquee = false;
    const std::vector<ImVec2> zone = g.marqueePts;
    g.marqueePts.clear();
    ImVec2 ra(0, 0), rb(0, 0);
    if (vp.selectShape == 1) {
        if (zone.size() < 2) return;
        ra = ImVec2(std::min(zone[0].x, zone[1].x), std::min(zone[0].y, zone[1].y));
        rb = ImVec2(std::max(zone[0].x, zone[1].x), std::max(zone[0].y, zone[1].y));
        if (rb.x - ra.x < 3.0f && rb.y - ra.y < 3.0f) return;  // clic sin arrastre
    } else if (zone.size() < 3) return;

    {
        const bool shape1 = vp.selectShape == 1;
        auto inside = [&](ImVec2 p) {
            return shape1 ? (p.x >= ra.x && p.x <= rb.x && p.y >= ra.y && p.y <= rb.y) : pointInPolygon(zone, p);
        };
        if (componentMarquee(selected, camera, vp, inside, io.KeyShift, io.KeyCtrl)) return;
    }
    const glm::mat4 vpm = camera.projectionMatrix(vp.w / vp.h, false) * camera.viewMatrix();
    auto project = [&](const glm::vec3& w, ImVec2& out) {
        const glm::vec4 c = vpm * glm::vec4(w, 1.0f);
        if (c.w <= 1e-4f) return false;
        out = ImVec2(vp.x + (c.x / c.w * 0.5f + 0.5f) * vp.w, vp.y + (0.5f - c.y / c.w * 0.5f) * vp.h);
        return true;
    };
    std::vector<SceneNode*> hits;
    for (const auto& c : root.children())
        forSubtree(*c, [&](SceneNode& n) {
            if (!nodeEditorVisible(&n)) return;
            std::vector<glm::vec3> local;
            if (n.primitive == PrimitiveType::Mesh && n.mesh) {
                const glm::vec3 lo = n.mesh->boundsMin, hi = n.mesh->boundsMax;
                for (int i = 0; i < 8; ++i) local.emplace_back(i & 1 ? hi.x : lo.x, i & 2 ? hi.y : lo.y, i & 4 ? hi.z : lo.z);
            } else if (n.primitive == PrimitiveType::Cube || n.primitive == PrimitiveType::Sphere) {
                for (int i = 0; i < 8; ++i) local.emplace_back(i & 1 ? 0.5f : -0.5f, i & 2 ? 0.5f : -0.5f, i & 4 ? 0.5f : -0.5f);
            } else if (n.primitive == PrimitiveType::Plane) {
                for (int i = 0; i < 4; ++i) local.emplace_back(i & 1 ? 0.5f : -0.5f, 0.0f, i & 2 ? 0.5f : -0.5f);
            }
            local.emplace_back(0.0f);  // el eje (luces y nulos solo tienen esto)
            const glm::mat4 w = n.worldMatrix();
            ImVec2 mn(FLT_MAX, FLT_MAX), mx(-FLT_MAX, -FLT_MAX);
            bool any = false, inside = false, allInside = true;
            for (const glm::vec3& l : local) {
                ImVec2 p;
                if (!project(glm::vec3(w * glm::vec4(l, 1.0f)), p)) continue;
                any = true;
                mn = ImVec2(std::min(mn.x, p.x), std::min(mn.y, p.y));
                mx = ImVec2(std::max(mx.x, p.x), std::max(mx.y, p.y));
                if (vp.selectShape == 2) {
                    if (pointInPolygon(zone, p)) inside = true;
                    else allInside = false;
                }
            }
            if (!any) return;
            // Tolerante (por defecto): basta con tocar la zona. Sin ella, el
            // objeto tiene que quedar dentro entero (opciones de la rueda).
            bool hit;
            if (vp.selectShape == 1)
                hit = g.selTolerant ? (mn.x <= rb.x && mx.x >= ra.x && mn.y <= rb.y && mx.y >= ra.y)
                                    : (mn.x >= ra.x && mx.x <= rb.x && mn.y >= ra.y && mx.y <= rb.y);
            else
                hit = g.selTolerant ? inside : allInside;
            if (hit) hits.push_back(&n);
        });

    if (io.KeyCtrl) {
        for (SceneNode* n : hits) g.selIds.erase(n->id());
        if (selected && !g.selIds.count(selected->id())) selected = firstSelected(root);
    } else {
        if (!io.KeyShift) g.selIds.clear();
        for (SceneNode* n : hits) g.selIds.insert(n->id());
        if (!hits.empty() && (!selected || !g.selIds.count(selected->id()))) selected = hits.front();
        if (hits.empty() && !io.KeyShift) selected = nullptr;
    }
    g.lastSelectedId = selected ? selected->id() : -1;
}

void newMaterial(std::vector<Material>& materials) {
    Material m;
    m.name = materials.empty() ? "Mat" : "Mat." + std::to_string(materials.size());
    materials.push_back(m);
    g.selectedMat = (int)materials.size() - 1;
    g.focus = AttrFocus::Material;
}

// --- Paletas de objetos (seccion 3 y 18.1 de la referencia) --------------------

struct PaletteItem {
    const char* name;
    Icon icon;
    PrimitiveType type; // None + !functional = deshabilitado
    bool functional;
};

void drawPalette(const char* id, const PaletteItem* items, int count, int columns, SceneNode& root,
                 SceneNode*& selected, SceneHistory& history) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4, 2));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(2, 1));
    if (!ImGui::BeginPopup(id)) {
        ImGui::PopStyleVar(2);
        return;
    }
    ImDrawList* dl = ImGui::GetWindowDrawList();
    // Tira rayada de arriba (la de "arrancar" la paleta en C4D): solo estetica.
    {
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float w = std::max(ImGui::GetContentRegionAvail().x, 120.0f);
        dl->AddRectFilled(p, ImVec2(p.x + w, p.y + 7), kTabOff);
        for (float x = p.x + 2; x < p.x + w - 2; x += 4)
            dl->AddLine(ImVec2(x, p.y + 6), ImVec2(x + 4, p.y + 1), kGrip);
        ImGui::Dummy(ImVec2(w, 8));
    }
    constexpr float kItemW = 168.0f, kItemH = 22.0f, kGutterW = 23.0f;
    if (ImGui::BeginTable("##pal", columns, ImGuiTableFlags_SizingFixedFit)) {
        for (int i = 0; i < count; ++i) {
            ImGui::TableNextColumn();
            const PaletteItem& it = items[i];
            ImGui::PushID(i);
            if (!it.functional) ImGui::BeginDisabled();
            const ImVec2 p = ImGui::GetCursorScreenPos();
            if (ImGui::Selectable("##it", false, 0, ImVec2(kItemW, kItemH))) {
                createObject(root, selected, history, it.type);
                ImGui::CloseCurrentPopup();
            }
            const bool hov = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled);
            // Franja oscura detras del icono, como la columna de iconos de R19.
            dl->AddRectFilled(p, ImVec2(p.x + kGutterW, p.y + kItemH), hov && it.functional ? kTabOn : kIconGutter);
            icons::draw(dl, it.icon, ImVec2(p.x + 2.5f, p.y + 2), 18, it.functional);
            dl->AddText(ImVec2(p.x + kGutterW + 5, p.y + 3), it.functional ? kTextCol : kTextDim, it.name);
            if (!it.functional) ImGui::EndDisabled();
            if (hov) {
                if (it.functional) ImGui::SetTooltip("%s\nAñadir Objeto %s  (Alt+Clic: como hijo del seleccionado)", it.name, it.name);
                else ImGui::SetTooltip("%s\nTodavía no implementado en Iron3D", it.name);
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ImGui::EndPopup();
    ImGui::PopStyleVar(2);
}

constexpr PrimitiveType kNone = PrimitiveType::None;

// Orden de cada paleta = el de las paletas de Cinema 4D R19 (fila a fila).
// Polygono Vacio no esta en la paleta de R19: solo en Crear > Objeto.
const PaletteItem kPrimitives[] = {
    {"Nulo", Icon::Null, kNone, true},
    {"Polígono Vacío", Icon::EmptyPolygon, kNone, false},
    {"Guía", Icon::Guide, kNone, false},
    {"Cono", Icon::Cone, kNone, false},
    {"Cubo", Icon::Cube, PrimitiveType::Cube, true},
    {"Cilindro", Icon::Cylinder, kNone, false},
    {"Disco", Icon::Disc, kNone, false},
    {"Plano", Icon::Plane, PrimitiveType::Plane, true},
    {"Polígono", Icon::Polygon, kNone, false},
    {"Esfera", Icon::Sphere, PrimitiveType::Sphere, true},
    {"Toroide", Icon::Torus, kNone, false},
    {"Cápsula", Icon::Capsule, kNone, false},
    {"Tanque de Petróleo", Icon::OilTank, kNone, false},
    {"Tubo", Icon::Tube, kNone, false},
    {"Pirámide", Icon::Pyramid, kNone, false},
    {"Platónico", Icon::Platonic, kNone, false},
    {"Figura", Icon::Figure, kNone, false},
    {"Terreno Fractal", Icon::Landscape, kNone, false},
    {"Relieve", Icon::Relief, kNone, false},
};

const PaletteItem kSplines[] = {
    {"Lápiz", Icon::SplPen, kNone, false},
    {"Dibujo", Icon::SplSketch, kNone, false},
    {"Suavizado Spline", Icon::SplSmooth, kNone, false},
    {"Spline Arco", Icon::SplArcTool, kNone, false},
    {"Arco", Icon::SplArc, kNone, false},
    {"Círculo", Icon::SplCircle, kNone, false},
    {"Hélice", Icon::SplHelix, kNone, false},
    {"n-Lados", Icon::SplNSide, kNone, false},
    {"Rectángulo", Icon::SplRectangle, kNone, false},
    {"Estrella", Icon::SplStar, kNone, false},
    {"Texto", Icon::SplText, kNone, false},
    {"Vectorizador", Icon::SplVectorizer, kNone, false},
    {"4-Lados", Icon::SplFourSide, kNone, false},
    {"Cisoide", Icon::SplCissoid, kNone, false},
    {"Rueda Dentada", Icon::SplCog, kNone, false},
    {"Cicloide", Icon::SplCycloid, kNone, false},
    {"Fórmula", Icon::SplFormula, kNone, false},
    {"Flor", Icon::SplFlower, kNone, false},
    {"Perfil", Icon::SplProfile, kNone, false},
    {"Sustraer Spline", Icon::SplSubtract, kNone, false},
    {"Unión de Splines", Icon::SplUnion, kNone, false},
    {"Spline Y", Icon::SplAnd, kNone, false},
    {"Spline O", Icon::SplOr, kNone, false},
    {"Intersección de Splines", Icon::SplIntersect, kNone, false},
};

const PaletteItem kGenerators[] = {
    {"Subdivisión Superficie", Icon::GenSubdiv, kNone, false},
    {"Extrusión", Icon::GenExtrude, kNone, false},
    {"Torno", Icon::GenLathe, kNone, false},
    {"Forro", Icon::GenLoft, kNone, false},
    {"Recorrido", Icon::GenSweep, kNone, false},
    {"Bezier", Icon::GenBezier, kNone, false},
};

// Grupo "Modelado" de la barra de R19 (icono de Matriz).
const PaletteItem kModeling[] = {
    {"Matriz", Icon::ModArray, kNone, false},
    {"Formación de Átomos", Icon::ModAtomArray, kNone, false},
    {"Booleano", Icon::ModBoole, kNone, false},
    {"Máscara de Spline", Icon::ModSplineMask, kNone, false},
    {"Conector", Icon::ModConnect, kNone, false},
    {"Simetría", Icon::ModSymmetry, kNone, false},
    {"Generador Python", Icon::ModPython, kNone, false},
    {"LOD", Icon::ModLod, kNone, false},
    {"Reducción de Polígonos", Icon::ModPolyReduction, kNone, false},
    {"Instancia", Icon::ModInstance, kNone, false},
    {"Metaball", Icon::ModMetaball, kNone, false},
};

const PaletteItem kMoGraph[] = {
    {"Clonador", Icon::MoCloner, kNone, false},
    {"Matriz", Icon::MoMatrix, kNone, false},
    {"Fractura", Icon::MoFracture, kNone, false},
    {"Fractura Voronoi", Icon::MoVoronoi, kNone, false},
    {"MoInstance", Icon::MoInstance, kNone, false},
    {"MoText", Icon::MoText, kNone, false},
    {"Trazador", Icon::MoTracer, kNone, false},
    {"MoSpline", Icon::MoSpline, kNone, false},
    {"MoExtrude", Icon::MoExtrude, kNone, false},
    {"PolyFX", Icon::MoPolyFx, kNone, false},
};

const PaletteItem kDeformers[] = {
    {"Doblar", Icon::DefBend, kNone, false},
    {"Hinchar", Icon::DefBulge, kNone, false},
    {"Sesgar", Icon::DefShear, kNone, false},
    {"Afilar", Icon::DefTaper, kNone, false},
    {"Enroscar", Icon::DefTwist, kNone, false},
    {"FFD", Icon::DefFfd, kNone, false},
    {"Malla", Icon::DefMesh, kNone, false},
    {"Aplastar y Estirar", Icon::DefSquash, kNone, false},
    {"Derretir", Icon::DefMelt, kNone, false},
    {"Explosión", Icon::DefExplosion, kNone, false},
    {"FX Explosión", Icon::DefExplosionFx, kNone, false},
    {"Romper", Icon::DefShatter, kNone, false},
    {"Corrección", Icon::DefCorrection, kNone, false},
    {"Jiggle", Icon::DefJiggle, kNone, false},
    {"Morph", Icon::DefMorph, kNone, false},
    {"Cubrir", Icon::DefShrinkWrap, kNone, false},
    {"Esferizar", Icon::DefSpherify, kNone, false},
    {"Superficie", Icon::DefSurface, kNone, false},
    {"Envoltura", Icon::DefWrap, kNone, false},
    {"Spline", Icon::DefSpline, kNone, false},
    {"Rail de Splines", Icon::DefSplineRail, kNone, false},
    {"Envolver Spline", Icon::DefSplineWrap, kNone, false},
    {"Cámara", Icon::DefCamera, kNone, false},
    {"Colisión", Icon::DefCollision, kNone, false},
    {"Desplazador", Icon::DefDisplacer, kNone, false},
    {"Fórmula", Icon::DefFormula, kNone, false},
    {"Viento", Icon::DefWind, kNone, false},
    {"Suavizado", Icon::DefSmoothing, kNone, false},
    {"Bisel", Icon::DefBevel, kNone, false},
};

const PaletteItem kScene[] = {
    {"Suelo", Icon::Floor, kNone, false},
    {"Cielo", Icon::SceSky, kNone, false},
    {"Entorno", Icon::SceEnvironment, kNone, false},
    {"Primer Plano", Icon::SceForeground, kNone, false},
    {"Fondo", Icon::SceBackground, kNone, false},
    {"Escenario", Icon::SceStage, kNone, false},
    {"Cielo Físico", Icon::ScePhysicalSky, kNone, false},
    {"Herramienta de Nubes", Icon::SceCloudTool, kNone, false},
    {"Grupo de Nubes", Icon::SceCloudGroup, kNone, false},
    {"Nube", Icon::SceCloud, kNone, false},
    {"Unir Nubes", Icon::SceCloudJoin, kNone, false},
    {"Césped", Icon::SceGrass, kNone, false},
};

const PaletteItem kCameras[] = {
    {"Cámara", Icon::Camera, kNone, false},
    {"Cámara Objetivo", Icon::CamTarget, kNone, false},
    {"Cámara Estéreo", Icon::CamStereo, kNone, false},
    {"Cámara de Movimiento", Icon::CamMotion, kNone, false},
    {"Morph de Cámara", Icon::CamMorph, kNone, false},
    {"Grúa", Icon::CamCrane, kNone, false},
};

const PaletteItem kLights[] = {
    {"Luz", Icon::Light, PrimitiveType::Light, true},
    {"Luz Objetivo", Icon::LgtTarget, kNone, false},
    {"Foco", Icon::LgtSpot, kNone, false},
    {"Luz IES", Icon::LgtIes, kNone, false},
    {"Luz Infinita", Icon::LgtInfinite, kNone, false},
    {"Luz de Área", Icon::LgtArea, kNone, false},
    {"Luz Solar", Icon::LgtSun, kNone, false},
    {"Luz PBR", Icon::LgtPbr, kNone, false},
    {"Herramienta de Iluminación", Icon::LgtTool, kNone, false},
};

template <size_t N>
int countOf(const PaletteItem (&)[N]) { return (int)N; }

// --- Atajos de teclado ---------------------------------------------------------------

void handleShortcuts(SceneNode& root, SceneNode*& selected, GizmoMode& mode, SceneHistory& history,
                     std::vector<Material>& materials, OrbitalCamera& camera, ViewportState& vp) {
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantTextInput) return;
    const bool ctrl = io.KeyCtrl, shift = io.KeyShift;
    auto pressed = [](ImGuiKey k) { return ImGui::IsKeyPressed(k, false); };

    if (!ctrl) {
        if (pressed(ImGuiKey_E)) { mode = GizmoMode::Translate; vp.selectTool = false; }
        if (pressed(ImGuiKey_R)) {
            if (shift) g.renderRequested = true;  // Mayus+R: Renderizar en Visor de Imagenes
            else { mode = GizmoMode::Rotate; vp.selectTool = false; }
        }
        if (pressed(ImGuiKey_W) && !shift) {
            vp.worldCoords = !vp.worldCoords;
            flashStatus(vp.worldCoords ? "Sistema de coordenadas: Mundo" : "Sistema de coordenadas: Objeto");
        }
        if (pressed(ImGuiKey_L) && !shift) {
            vp.axisMode = !vp.axisMode;
            flashStatus(vp.axisMode ? "Activar Eje: se mueve el eje, no el objeto" : "Activar Eje: desactivado");
        }
        if (pressed(ImGuiKey_T)) { mode = GizmoMode::Scale; vp.selectTool = false; }
        if (pressed(ImGuiKey_9)) { vp.selectTool = true; vp.selectShape = 0; }
        if (pressed(ImGuiKey_0)) { vp.selectTool = true; vp.selectShape = 1; }
        if (pressed(ImGuiKey_8)) { vp.selectTool = true; vp.selectShape = 2; }
        if (pressed(ImGuiKey_H)) {
            if (shift) camera.resetView();
            else frameNodes(camera, root, false);
        }
        if (pressed(ImGuiKey_O) && selected) frameNodes(camera, *selected, true);
        if (pressed(ImGuiKey_S) && !shift && !io.KeyAlt) g.pendingCmd = UiCmd::FrameSelected; // Encuadrar Seleccionados
        if (pressed(ImGuiKey_C) && !shift && !io.KeyAlt) g.pendingCmd = UiCmd::MakeEditable;  // Hacer Editable
        if (pressed(ImGuiKey_C) && shift) g.showCommander = true;                              // Mayus+C: Commander
        if (pressed(ImGuiKey_Space) && g.lastTool >= 0) {                                      // ultima herramienta
            const int t = g.lastTool;
            vp.selectTool = t >= 3;
            if (t >= 3) vp.selectShape = t - 3;
            else mode = (GizmoMode)t;
        }
        if (pressed(ImGuiKey_Delete) || pressed(ImGuiKey_Backspace)) {
            if (g.focus == AttrFocus::Material && g.selectedMat >= 0 && (size_t)g.selectedMat < materials.size()) {
                history.commitBefore(root);
                fixupMaterialIndicesAfterRemoval(root, g.selectedMat);
                materials.erase(materials.begin() + g.selectedMat);
                g.selectedMat = -1;
            } else if (!deleteComponents(root, selected, history)) {
                deleteSelected(root, selected, history);
            }
        }
        if (pressed(ImGuiKey_F8)) {
            if (shift) g.browserOpen = !g.browserOpen; // Mayus+F8: Navegador de Contenido
            else g.playDir = g.playDir == 1 ? 0 : 1;
        }
        if (shift && pressed(ImGuiKey_F1)) g.browserOpen = false; // Mayus+F1: Gestor de Objetos
        // F1-F4: Vista 1-4 de C4D (Perspectiva, Superior, Derecha, Frontal);
        // F5: las cuatro a la vez (otra vez F5 maximiza la activa).
        if (!shift && !io.KeyAlt) {
            if (pressed(ImGuiKey_F1)) selectView(camera, CamView::Perspective);
            if (pressed(ImGuiKey_F2)) selectView(camera, CamView::Top);
            if (pressed(ImGuiKey_F3)) selectView(camera, CamView::Right);
            if (pressed(ImGuiKey_F4)) selectView(camera, CamView::Front);
            if (pressed(ImGuiKey_F5)) toggleQuad(camera);
        }
        if (pressed(ImGuiKey_F6)) g.playDir = g.playDir == -1 ? 0 : -1;
        if (pressed(ImGuiKey_F7)) g.playDir = 0;
        if (pressed(ImGuiKey_F9) && !shift) recordActiveObjects(root, selected, &history);  // Grabar Objetos Activos
        if (pressed(ImGuiKey_G)) {
            if (io.KeyAlt) groupObjects(root, selected, history, nullptr);       // Alt+G: Agrupar Objetos
            else if (shift && g.objMgrHovered) {                                  // Mayus+G: Expandir Grupo
                if (selected && !selected->children().empty()) dissolveNode(root, selected, history, selected);
            } else g.frame = shift ? (float)g.maxFrame : std::min((float)g.maxFrame, std::floor(g.frame) + 1);
        }
        if (pressed(ImGuiKey_F)) g.frame = shift ? (float)g.minFrame : std::max((float)g.minFrame, std::ceil(g.frame) - 1);
        if (pressed(ImGuiKey_Escape)) selected = nullptr;
    } else {
        if (pressed(ImGuiKey_Z) && !shift) {
            if (history.undo(root)) selected = nullptr;
        }
        if (pressed(ImGuiKey_Y) || (pressed(ImGuiKey_Z) && shift)) {
            if (history.redo(root)) selected = nullptr;
        }
        if (pressed(ImGuiKey_C) && selected) g.clipboardNodeId = selected->id();
        if (pressed(ImGuiKey_A)) g.pendingCmd = UiCmd::SelectAll;  // Seleccionar Todo (objetos)
        if (pressed(ImGuiKey_I)) g.focus = AttrFocus::Project;  // Informacion del Proyecto
        if (pressed(ImGuiKey_V)) {
            SceneNode* src = g.clipboardNodeId >= 0 ? findById(root, g.clipboardNodeId) : nullptr;
            if (src && src->parent()) {
                history.commitBefore(root);
                selected = duplicateSubtree(*src->parent(), *src);
            }
        }
        if (pressed(ImGuiKey_R)) vp.renderView = true;
        if (pressed(ImGuiKey_B)) g.showRenderSettings = true;  // Editar Configuracion de Render
        if (pressed(ImGuiKey_N)) newScene(root, selected, history, materials);
        if (pressed(ImGuiKey_O)) {
            if (shift) mergeSceneFile(root, selected, history, materials);
            else openSceneFile(root, selected, history, materials, camera);
        }
        if (pressed(ImGuiKey_S)) { g.showScene = true; g.sceneLoadMode = false; }
        if (pressed(ImGuiKey_E)) g.showPrefs = true;
        if (pressed(ImGuiKey_D)) { g.focus = AttrFocus::Project; }
        if (pressed(ImGuiKey_F9)) {  // Fotogramas Clave Automaticos
            g.autoKey = !g.autoKey;
            flashStatus(g.autoKey ? "Fotogramas Clave Automáticos: activados" : "Fotogramas Clave Automáticos: desactivados");
        }
        if (pressed(ImGuiKey_F)) jumpToKey(-1);  // Ir a la Clave Anterior
        if (pressed(ImGuiKey_G)) jumpToKey(1);   // Ir a la Clave Siguiente
    }
}

// --- Ordenes diferidas, Commander y menu contextual del visor -------------------------

void disabledItems(std::initializer_list<std::pair<const char*, const char*>> items);

void flashStatus(const std::string& text) {
    g.status = text;
    g.statusUntil = ImGui::GetTime() + 3.0;
}

// Encuadrar Seleccionados (S / Alt+O): toda la seleccion, no solo el activo.
void frameSelection(OrbitalCamera& camera, SceneNode& root, SceneNode* selected) {
    std::vector<SceneNode*> nodes;
    collectSelected(root, nodes);
    if (nodes.empty() && selected) nodes.push_back(selected);
    if (nodes.empty()) return;
    glm::vec3 mn(0.0f), mx(0.0f);
    bool any = false;
    for (SceneNode* n : nodes) accumulateBounds(*n, mn, mx, any);
    if (!any) {  // solo nulos: se encuadra su posicion
        for (SceneNode* n : nodes) {
            const glm::vec3 p(n->worldMatrix()[3]);
            mn = any ? glm::min(mn, p) : p;
            mx = any ? glm::max(mx, p) : p;
            any = true;
        }
    }
    camera.frame((mn + mx) * 0.5f, std::max(0.25f, glm::length(mx - mn) * 0.5f));
}

void runPendingCmd(SceneNode& root, SceneNode*& selected, SceneHistory& history, OrbitalCamera& camera) {
    if (g.pendingCmd == UiCmd::None && !g.cmdQueue.empty()) {
        g.pendingCmd = g.cmdQueue.front();
        g.cmdQueue.erase(g.cmdQueue.begin());
    }
    const UiCmd cmd = g.pendingCmd;
    g.pendingCmd = UiCmd::None;
    switch (cmd) {
        case UiCmd::MakeEditable: {
            const int n = makeEditable(root, selected, history);
            if (n == 0) flashStatus("Hacer Editable: selecciona un Cubo, Esfera o Plano (los demás ya son editables o no tienen malla).");
            else flashStatus(n == 1 ? "Hacer Editable: 1 objeto convertido a Objeto Poligonal."
                                    : "Hacer Editable: " + std::to_string(n) + " objetos convertidos a Objeto Poligonal.");
            break;
        }
        case UiCmd::FrameSelected: frameSelection(camera, root, selected); break;
        case UiCmd::FrameAll: frameNodes(camera, root, false); break;
        case UiCmd::ResetView: camera.resetView(); break;
        case UiCmd::Group: groupObjects(root, selected, history, nullptr); break;
        case UiCmd::SelectChildren:
            if (selected) {
                std::vector<SceneNode*> roots;
                collectSelected(root, roots);
                if (roots.empty()) roots.push_back(selected);
                for (SceneNode* r : roots) forSubtree(*r, [](SceneNode& n) { g.selIds.insert(n.id()); });
            }
            break;
        case UiCmd::SelectAll: selectAllObjects(root, selected); break;
        case UiCmd::ViewPersp: selectView(camera, CamView::Perspective); break;
        case UiCmd::ViewTop: selectView(camera, CamView::Top); break;
        case UiCmd::ViewRight: selectView(camera, CamView::Right); break;
        case UiCmd::ViewFront: selectView(camera, CamView::Front); break;
        case UiCmd::ViewLeft: selectView(camera, CamView::Left); break;
        case UiCmd::ViewBack: selectView(camera, CamView::Back); break;
        case UiCmd::ViewBottom: selectView(camera, CamView::Bottom); break;
        case UiCmd::ViewQuad: toggleQuad(camera); break;
        case UiCmd::ClearAnimation: {
            std::vector<SceneNode*> nodes;
            collectSelected(root, nodes);
            if (nodes.empty() && selected) nodes.push_back(selected);
            history.commitBefore(root);
            for (SceneNode* n : nodes) n->animPos = n->animRot = n->animScale = AnimTrack{};
            flashStatus("Animación eliminada.");
            break;
        }
        case UiCmd::CurrentState: {
            const int n = currentStateToObject(root, selected, history);
            flashStatus(n ? "Estado Actual a Objeto: " + std::to_string(n) + (n == 1 ? " copia poligonal creada." : " copias poligonales creadas.")
                          : std::string("Estado Actual a Objeto: la selección no tiene geometría."));
            break;
        }
        case UiCmd::Connect:
        case UiCmd::ConnectDelete: {
            const int n = connectObjects(root, selected, history, cmd == UiCmd::ConnectDelete);
            flashStatus(n ? "Conectar Objetos: " + std::to_string(n) + " mallas unidas en un Objeto Polígono."
                          : std::string("Conectar Objetos: la selección no tiene geometría."));
            break;
        }
        case UiCmd::None: break;
    }
}

// Commander (Mayus+C): buscador de ordenes por nombre, como en C4D. Enter o
// clic ejecuta la primera/seleccionada; flechas para moverse; Esc cierra.
void drawCommander(SceneNode& root, SceneNode*& selected, GizmoMode& mode, SceneHistory& history,
                   std::vector<Material>& materials, OrbitalCamera& camera, ViewportState& vp) {
    if (!g.showCommander) return;
    struct Cmd { const char* name; const char* key; std::function<void()> run; bool enabled; };
    const bool hasSel = selected != nullptr;
    auto tool = [&](int t) {
        vp.selectTool = t >= 3;
        if (t >= 3) vp.selectShape = t - 3;
        else mode = (GizmoMode)t;
    };
    const std::vector<Cmd> cmds = {
        {"Hacer Editable", "C", [] { g.pendingCmd = UiCmd::MakeEditable; }, canMakeEditable(root, selected)},
        {"Mover", "E", [&] { tool(0); }, true},
        {"Rotar", "R", [&] { tool(1); }, true},
        {"Escalar", "T", [&] { tool(2); }, true},
        {"Selección en Vivo", "9", [&] { tool(3); }, true},
        {"Selección Rectangular", "0", [&] { tool(4); }, true},
        {"Selección con Lazo", "8", [&] { tool(5); }, true},
        {"Seleccionar Todo", "Ctrl+A", [] { g.pendingCmd = UiCmd::SelectAll; }, true},
        {"Estado Actual a Objeto", "", [] { g.pendingCmd = UiCmd::CurrentState; }, hasSel},
        {"Conectar Objetos", "", [] { g.pendingCmd = UiCmd::Connect; }, hasSel},
        {"Conectar Objetos + Eliminar", "", [] { g.pendingCmd = UiCmd::ConnectDelete; }, hasSel},
        {"Encuadrar Geometría", "H", [] { g.pendingCmd = UiCmd::FrameAll; }, true},
        {"Encuadrar Todo", "Alt+H", [] { g.pendingCmd = UiCmd::FrameAll; }, true},
        {"Encuadrar Seleccionados", "S", [] { g.pendingCmd = UiCmd::FrameSelected; }, hasSel},
        {"Encuadre por Defecto", "Mayús+H", [] { g.pendingCmd = UiCmd::ResetView; }, true},
        {"Vista Perspectiva", "F1", [] { g.pendingCmd = UiCmd::ViewPersp; }, true},
        {"Vista Superior", "F2", [] { g.pendingCmd = UiCmd::ViewTop; }, true},
        {"Vista Derecha", "F3", [] { g.pendingCmd = UiCmd::ViewRight; }, true},
        {"Vista Frontal", "F4", [] { g.pendingCmd = UiCmd::ViewFront; }, true},
        {"Todas las Vistas", "F5", [] { g.pendingCmd = UiCmd::ViewQuad; }, true},
        {"Renderizar en Visor de Imágenes", "Mayús+R", [] { g.renderRequested = true; }, true},
        {"Grabar Objetos Activos", "F9", [] { g.recordRequested = true; }, hasSel},
        {"Fotogramas Clave Automáticos", "Ctrl+F9", [] { g.autoKey = !g.autoKey; }, true},
        {"Eliminar Animación del Objeto", "", [] { g.pendingCmd = UiCmd::ClearAnimation; }, hasSel},
        {"Editar Configuración de Render...", "Ctrl+B", [] { g.showRenderSettings = true; }, true},
        {"Visor de Imágenes...", "", [] { g.showPictureViewer = true; }, true},
        {"Sistema de Coordenadas de Mundo", "W", [] { if (g_vp) g_vp->worldCoords = !g_vp->worldCoords; }, true},
        {"Activar Eje", "L", [] { if (g_vp) g_vp->axisMode = !g_vp->axisMode; }, true},
        {"Vista Izquierda", "", [] { g.pendingCmd = UiCmd::ViewLeft; }, true},
        {"Vista Trasera", "", [] { g.pendingCmd = UiCmd::ViewBack; }, true},
        {"Vista Inferior", "", [] { g.pendingCmd = UiCmd::ViewBottom; }, true},
        {"Agrupar Objetos", "Alt+G", [] { g.pendingCmd = UiCmd::Group; }, hasSel},
        {"Seleccionar Hijos", "", [] { g.pendingCmd = UiCmd::SelectChildren; }, hasSel},
        {"Deseleccionar Todo", "Esc", [&] { selected = nullptr; }, hasSel},
        {"Eliminar", "Supr", [&] { deleteSelected(root, selected, history); }, hasSel},
        {"Deshacer", "Ctrl+Z", [&] { if (history.undo(root)) selected = nullptr; }, true},
        {"Rehacer", "Ctrl+Y", [&] { if (history.redo(root)) selected = nullptr; }, true},
        {"Cubo", "", [&] { createObject(root, selected, history, PrimitiveType::Cube); }, true},
        {"Esfera", "", [&] { createObject(root, selected, history, PrimitiveType::Sphere); }, true},
        {"Plano", "", [&] { createObject(root, selected, history, PrimitiveType::Plane); }, true},
        {"Luz", "", [&] { createObject(root, selected, history, PrimitiveType::Light); }, true},
        {"Nulo", "", [&] { createObject(root, selected, history, PrimitiveType::None); }, true},
        {"Nuevo Material", "", [&] { newMaterial(materials); }, true},
        {"Nuevo Proyecto", "Ctrl+N", [&] { newScene(root, selected, history, materials); }, true},
        {"Abrir...", "Ctrl+O", [&] { openSceneFile(root, selected, history, materials, camera); }, true},
        {"Fusionar...", "Ctrl+Mayús+O", [&] { mergeSceneFile(root, selected, history, materials); }, true},
        {"Guardar Proyecto", "Ctrl+S", [] { g.showScene = true; g.sceneLoadMode = false; }, true},
        {"Renderizar Vista Activa", "Ctrl+R", [&] { vp.renderView = true; }, true},
        {"Preferencias...", "Ctrl+E", [] { g.showPrefs = true; }, true},
        {"Configuración del Proyecto", "Ctrl+D", [] { g.focus = AttrFocus::Project; }, true},
        {"Navegador de Contenido", "Mayús+F8", [] { g.browserOpen = true; }, true},
        {"Gestor de Objetos", "Mayús+F1", [] { g.browserOpen = false; }, true},
        {"Reproducir Hacia Delante", "F8", [] { g.playDir = g.playDir == 1 ? 0 : 1; }, true},
        {"Detener", "F7", [] { g.playDir = 0; }, true},
        {"Ir al Inicio", "Mayús+F", [] { g.frame = (float)g.minFrame; }, true},
        {"Ir al Final", "Mayús+G", [] { g.frame = (float)g.maxFrame; }, true},
    };

    // Coincidencia sin mayusculas ni tildes simples: cada palabra del filtro
    // tiene que aparecer en el nombre.
    auto fold = [](std::string s) {
        std::string out;
        for (size_t i = 0; i < s.size(); ++i) {
            const unsigned char c = (unsigned char)s[i];
            if (c == 0xC3 && i + 1 < s.size()) {  // a e i o u n con tilde (UTF-8)
                const unsigned char d = (unsigned char)s[++i];
                const char* map = "aaaaaaaceeeeiiiidnooooo/ouuuuyty";
                out += (d >= 0xA0 && d <= 0xBF) ? map[d - 0xA0] : (d >= 0x80 && d <= 0x9F) ? map[d - 0x80] : '?';
            } else out += (char)std::tolower(c);
        }
        return out;
    };
    const std::string filter = fold(g.commanderBuf);
    std::vector<const Cmd*> hits;
    for (const Cmd& c : cmds) {
        const std::string n = fold(c.name);
        bool ok = true;
        size_t p = 0;
        while (ok && p < filter.size()) {
            const size_t e = filter.find(' ', p);
            const std::string w = filter.substr(p, e == std::string::npos ? std::string::npos : e - p);
            if (!w.empty() && n.find(w) == std::string::npos) ok = false;
            p = e == std::string::npos ? filter.size() : e + 1;
        }
        if (ok) hits.push_back(&c);
    }
    g.commanderSel = hits.empty() ? 0 : std::clamp(g.commanderSel, 0, (int)hits.size() - 1);

    const ImGuiViewport* mv = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(mv->Pos.x + mv->Size.x * 0.5f, mv->Pos.y + 90.0f), ImGuiCond_Appearing, ImVec2(0.5f, 0.0f));
    ImGui::SetNextWindowSize(ImVec2(420, 0));
    bool open = true;
    if (ImGui::Begin("Commander", &open, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
                                            ImGuiWindowFlags_AlwaysAutoResize)) {
        if (ImGui::IsWindowAppearing()) {
            std::snprintf(g.commanderBuf, sizeof(g.commanderBuf), "%s", g.commanderPreset.c_str());
            g.commanderPreset.clear();
            g.commanderSel = 0;
            ImGui::SetKeyboardFocusHere();
        }
        ImGui::SetNextItemWidth(-1);
        const bool enter = ImGui::InputTextWithHint("##cmdq", "Buscar orden...", g.commanderBuf, sizeof(g.commanderBuf),
                                                    ImGuiInputTextFlags_EnterReturnsTrue);
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) g.commanderSel = std::min(g.commanderSel + 1, (int)hits.size() - 1);
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) g.commanderSel = std::max(g.commanderSel - 1, 0);
        const Cmd* chosen = nullptr;
        ImGui::BeginChild("##cmdlist", ImVec2(0, 260), ImGuiChildFlags_Border);
        for (int i = 0; i < (int)hits.size(); ++i) {
            const Cmd& c = *hits[i];
            ImGui::BeginDisabled(!c.enabled);
            if (ImGui::Selectable(c.name, i == g.commanderSel)) chosen = &c;
            if (i == g.commanderSel && ImGui::IsItemVisible() == false) ImGui::SetScrollHereY();
            if (c.key[0]) {
                ImGui::SameLine(300);
                ImGui::TextDisabled("%s", c.key);
            }
            ImGui::EndDisabled();
        }
        if (hits.empty()) ImGui::TextDisabled("Ninguna orden coincide.");
        ImGui::EndChild();
        if (enter && !hits.empty() && hits[g.commanderSel]->enabled) chosen = hits[g.commanderSel];
        if (chosen) {
            chosen->run();
            open = false;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) open = false;
    }
    ImGui::End();
    if (!open) g.showCommander = false;
}

// Clic derecho en el visor (sin Alt y sin arrastrar): menu contextual del
// Modo Modelo de C4D R19, con lo que Iron3D ya sabe hacer activo.
void drawViewportContextMenu(SceneNode& root, SceneNode*& selected, GizmoMode& mode, SceneHistory& history,
                             ViewportState& vp) {
    ImGuiIO& io = ImGui::GetIO();
    const ImVec2 c = io.MouseClickedPos[ImGuiMouseButton_Right];
    const bool startedInVp = c.x >= vp.x && c.y >= vp.y && c.x < vp.x + vp.w && c.y < vp.y + vp.h;
    if (ImGui::IsMouseReleased(ImGuiMouseButton_Right) && startedInVp && !io.KeyAlt &&
        io.MouseDragMaxDistanceSqr[ImGuiMouseButton_Right] < 16.0f && !ImGui::IsAnyItemHovered() &&
        !ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow))
        ImGui::OpenPopup("##vpctx");
    // --ui-cmd=vp-menu: como un clic derecho en el visor, unos frames tras
    // arrancar (en los primeros, el cambio de foco inicial cierra los popups).
    if (g.openVpMenu > 0 && --g.openVpMenu == 0) {
        ImGui::SetNextWindowPos(ImVec2(vp.x + vp.w * 0.5f, vp.y + vp.h * 0.3f));
        ImGui::OpenPopup("##vpctx");
    }
    if (!ImGui::BeginPopup("##vpctx")) return;
    const bool hasSel = selected != nullptr;
    if (iconMenuItem(Icon::Undo, "Deshacer", "Ctrl+Z", false, history.canUndo()))
        if (history.undo(root)) selected = nullptr;
    ImGui::Separator();
    if (ImGui::MenuItem("Encuadrar Seleccionados", "S", false, hasSel)) g.pendingCmd = UiCmd::FrameSelected;
    if (ImGui::MenuItem("Encuadrar Geometría", "H")) g.pendingCmd = UiCmd::FrameAll;
    ImGui::Separator();
    if (iconMenuItem(Icon::MakeEditable, "Hacer Editable", "C", false, canMakeEditable(root, selected)))
        g.pendingCmd = UiCmd::MakeEditable;
    if (ImGui::MenuItem("Estado Actual a Objeto", nullptr, false, hasSel)) g.pendingCmd = UiCmd::CurrentState;
    {
        bool gc = false;
        if (gearMenuItem(nullptr, "Conectar Objetos", nullptr, false, hasSel, &gc)) g.pendingCmd = UiCmd::Connect;
        if (gc) openToolOptions((int)UiState::OptWin::Connect);
    }
    if (ImGui::MenuItem("Conectar+Eliminar", nullptr, false, hasSel)) g.pendingCmd = UiCmd::ConnectDelete;
    if (iconMenuItem(Icon::Null, "Agrupar Objetos", "Alt+G", false, hasSel)) g.pendingCmd = UiCmd::Group;
    ImGui::Separator();
    if (ImGui::BeginMenu("Nueva Etiqueta", false)) ImGui::EndMenu();
    if (ImGui::BeginMenu("Restaurar Selección", false)) ImGui::EndMenu();
    if (ImGui::MenuItem("Seleccionar Hijos", nullptr, false, hasSel && !selected->children().empty()))
        g.pendingCmd = UiCmd::SelectChildren;
    ImGui::Separator();
    disabledItems({{"Mostrar Pistas...", ""}, {"Mostrar F-Curves...", ""}, {"Mostrar Movimientos...", ""}});
    ImGui::Separator();
    if (iconMenuItem(Icon::Move, "Mover", "E", !vp.selectTool && mode == GizmoMode::Translate)) {
        mode = GizmoMode::Translate;
        vp.selectTool = false;
    }
    if (iconMenuItem(Icon::Scale, "Escalar", "T", !vp.selectTool && mode == GizmoMode::Scale)) {
        mode = GizmoMode::Scale;
        vp.selectTool = false;
    }
    if (iconMenuItem(Icon::Rotate, "Rotar", "R", !vp.selectTool && mode == GizmoMode::Rotate)) {
        mode = GizmoMode::Rotate;
        vp.selectTool = false;
    }
    ImGui::Separator();
    if (iconMenuItem(Icon::LiveSelect, "Selección en Vivo", "9", vp.selectTool && vp.selectShape == 0)) { vp.selectTool = true; vp.selectShape = 0; }
    {
        bool gr = false, gl = false;
        const Icon ir = Icon::MnuRectSelect, il = Icon::MnuLassoSelect;
        if (gearMenuItem(&ir, "Selección Rectangular", "0", vp.selectTool && vp.selectShape == 1, true, &gr)) { vp.selectTool = true; vp.selectShape = 1; }
        if (gearMenuItem(&il, "Selección con Lazo", "8", vp.selectTool && vp.selectShape == 2, true, &gl)) { vp.selectTool = true; vp.selectShape = 2; }
        if (gr) { vp.selectTool = true; vp.selectShape = 1; openToolOptions((int)UiState::OptWin::RectSel); }
        if (gl) { vp.selectTool = true; vp.selectShape = 2; openToolOptions((int)UiState::OptWin::LassoSel); }
    }
    ImGui::EndPopup();
}

// --- Menu principal ------------------------------------------------------------------

void disabledItems(std::initializer_list<std::pair<const char*, const char*>> items) {
    for (const auto& it : items) {
        if (!it.first) ImGui::Separator();
        else ImGui::MenuItem(it.first, it.second, false, false);
    }
}

void drawMainMenu(SceneNode& root, SceneNode*& selected, GizmoMode& mode, SceneHistory& history,
                  std::vector<Material>& materials, OrbitalCamera& camera, ViewportState& vp) {
    if (!ImGui::BeginMainMenuBar()) return;

    if (ImGui::BeginMenu("Archivo")) {
        if (iconMenuItem(Icon::MnuNew, "Nuevo", "Ctrl+N")) newScene(root, selected, history, materials);
        if (iconMenuItem(Icon::MnuOpen, "Abrir...", "Ctrl+O")) openSceneFile(root, selected, history, materials, camera);
        if (ImGui::MenuItemEx("Fusionar...", kIconGutterText, "Ctrl+Mayús+O"))
            mergeSceneFile(root, selected, history, materials);
        disabledItems({{"Volver a Archivo Guardado...", ""}, {nullptr, nullptr},
                       {"Cerrar", "Ctrl+F4"}, {"Cerrar Todos", "Ctrl+Mayús+F4"}, {nullptr, nullptr}});
        if (iconMenuItem(Icon::MnuSave, "Guardar", "Ctrl+S")) { g.showScene = true; g.sceneLoadMode = false; }
        if (iconMenuItem(Icon::MnuSaveAs, "Guardar como...", "Ctrl+Mayús+S")) { g.showScene = true; g.sceneLoadMode = false; }
        disabledItems({{"Guardar incrementalmente...", ""}, {"Guardar Todo", ""},
                       {"Guardar Proyecto con su Contenido...", ""}});
        if (ImGui::MenuItemEx("Guardar Objetos Seleccionados como...", kIconGutterText, nullptr, false,
                              selected != nullptr && cinewareAvailable()))
            exportSelectedC4D(root, selected, materials);
        ImGui::Separator();
        if (iconBeginMenu(Icon::MnuExport, "Exportar")) {
            if (ImGui::MenuItem("Cinema 4D (*.c4d)...", nullptr, false, cinewareAvailable()))
                exportSceneC4D(root, materials);
            if (!cinewareAvailable() && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("Esta compilación no lleva el Cineware SDK de MAXON");
            disabledItems({{"Wavefront (*.obj)", ""}, {"FBX (*.fbx)", ""}, {"Alembic (*.abc)", ""},
                           {"STL (*.stl)", ""}, {"Collada 1.4 (*.dae)", ""}});
            ImGui::EndMenu();
        }
        ImGui::BeginMenuEx("Archivos recientes", kIconGutterText, false);
        ImGui::Separator();
        if (iconMenuItem(Icon::MnuExit, "Salir", "Alt+F4")) vp.requestQuit = true;
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Edición")) {
        if (iconMenuItem(Icon::Undo, "Deshacer", "Ctrl+Z", false, history.canUndo()))
            if (history.undo(root)) selected = nullptr;
        if (iconMenuItem(Icon::Redo, "Rehacer", "Ctrl+Y", false, history.canRedo()))
            if (history.redo(root)) selected = nullptr;
        ImGui::Separator();
        iconMenuItem(Icon::MnuCut, "Cortar", "Ctrl+X", false, false);
        if (iconMenuItem(Icon::MnuCopy, "Copiar", "Ctrl+C", false, selected != nullptr)) g.clipboardNodeId = selected->id();
        if (iconMenuItem(Icon::MnuPaste, "Pegar", "Ctrl+V", false, g.clipboardNodeId >= 0)) {
            SceneNode* src = findById(root, g.clipboardNodeId);
            if (src && src->parent()) {
                history.commitBefore(root);
                selected = duplicateSubtree(*src->parent(), *src);
            }
        }
        if (iconMenuItem(Icon::MnuDelete, "Eliminar", "Supr", false, selected != nullptr)) deleteSelected(root, selected, history);
        ImGui::Separator();
        if (iconMenuItem(Icon::MnuSelectAll, "Seleccionar Todo", "Ctrl+A")) g.pendingCmd = UiCmd::SelectAll;
        if (iconMenuItem(Icon::MnuDeselect, "Deseleccionar Todo", "Esc", false, selected != nullptr)) selected = nullptr;
        ImGui::Separator();
        if (iconMenuItem(Icon::MnuProject, "Configuración del Proyecto...", "Ctrl+D")) g.focus = AttrFocus::Project;
        ImGui::MenuItemEx("Escalar Proyecto...", kIconGutterText, "", false, false);
        if (iconMenuItem(Icon::MnuPrefs, "Preferencias...", "Ctrl+E")) g.showPrefs = true;
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Crear")) {
        if (iconBeginMenu(Icon::Cube, "Objeto")) {
            for (const auto& it : kPrimitives)
                if (iconMenuItem(it.icon, it.name, nullptr, false, it.functional)) createObject(root, selected, history, it.type);
            ImGui::EndMenu();
        }
        auto stubMenu = [](Icon icon, const char* name, const PaletteItem* items, int n) {
            if (iconBeginMenu(icon, name)) {
                for (int i = 0; i < n; ++i) iconMenuItem(items[i].icon, items[i].name, nullptr, false, false);
                ImGui::EndMenu();
            }
        };
        stubMenu(Icon::Spline, "Spline", kSplines, countOf(kSplines));
        stubMenu(Icon::Nurbs, "Generadores", kGenerators, countOf(kGenerators));
        stubMenu(Icon::ModArray, "Modelado", kModeling, countOf(kModeling));
        stubMenu(Icon::Cloner, "MoGraph", kMoGraph, countOf(kMoGraph));
        stubMenu(Icon::Deformer, "Deformador", kDeformers, countOf(kDeformers));
        stubMenu(Icon::Floor, "Escena", kScene, countOf(kScene));
        ImGui::BeginMenuEx("Física", kIconGutterText, false);
        stubMenu(Icon::Camera, "Cámara", kCameras, countOf(kCameras));
        if (iconBeginMenu(Icon::Light, "Luz")) {
            for (const auto& it : kLights)
                if (iconMenuItem(it.icon, it.name, nullptr, false, it.functional)) createObject(root, selected, history, it.type);
            ImGui::EndMenu();
        }
        if (iconBeginMenu(Icon::MnuNewMaterial, "Materiales")) {
            if (iconMenuItem(Icon::MnuNewMaterial, "Nuevo Material")) newMaterial(materials);
            iconMenuItem(Icon::MnuNewPbrMaterial, "Nuevo Material PBR", "", false, false);
            iconMenuItem(Icon::MnuLoadMaterial, "Cargar Materiales...", "", false, false);
            ImGui::EndMenu();
        }
        ImGui::BeginMenuEx("Etiqueta", kIconGutterText, false);
        ImGui::BeginMenuEx("Sonido", kIconGutterText, false);
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Selección")) {
        ImGui::BeginMenuEx("Filtro de Selección", kIconGutterText, false);
        if (iconMenuItem(Icon::LiveSelect, "Selección en Vivo", "9", vp.selectTool && vp.selectShape == 0)) { vp.selectTool = true; vp.selectShape = 0; }
        {
            bool gr = false, gl = false;
            const Icon ir = Icon::MnuRectSelect, il = Icon::MnuLassoSelect;
            if (gearMenuItem(&ir, "Selección Rectangular", "0", vp.selectTool && vp.selectShape == 1, true, &gr)) { vp.selectTool = true; vp.selectShape = 1; }
            if (gearMenuItem(&il, "Selección con Lazo", "8", vp.selectTool && vp.selectShape == 2, true, &gl)) { vp.selectTool = true; vp.selectShape = 2; }
            if (gr) { vp.selectTool = true; vp.selectShape = 1; openToolOptions((int)UiState::OptWin::RectSel); }
            if (gl) { vp.selectTool = true; vp.selectShape = 2; openToolOptions((int)UiState::OptWin::LassoSel); }
        }
        iconMenuItem(Icon::MnuPolySelect, "Selección de Polígono", "", false, false);
        ImGui::Separator();
        iconMenuItem(Icon::MnuLoop, "Selección de Bucle", "U~L", false, false);
        for (const auto& it : std::initializer_list<std::pair<const char*, const char*>>{
                 {"Selección de Anillo", "U~B"}, {"Selección Contorno", "U~Q"}, {"Rellenar Selección", "U~F"}})
            ImGui::MenuItemEx(it.first, kIconGutterText, it.second, false, false);
        ImGui::Separator();
        if (iconMenuItem(Icon::MnuSelectAll, "Seleccionar Todo", "Ctrl+A")) g.pendingCmd = UiCmd::SelectAll;
        if (iconMenuItem(Icon::MnuDeselect, "Deseleccionar Todo", "Esc", false, selected != nullptr)) selected = nullptr;
        for (const auto& it : std::initializer_list<std::pair<const char*, const char*>>{
                 {"Invertir Selección", "U~I"}, {"Seleccionar Conectados", "U~W"},
                 {"Aumentar Selección", "U~Y"}, {"Reducir Selección", "U~K"}})
            ImGui::MenuItemEx(it.first, kIconGutterText, it.second, false, false);
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Herramientas")) {
        if (iconMenuItem(Icon::Move, "Mover", "E", !vp.selectTool && mode == GizmoMode::Translate)) { mode = GizmoMode::Translate; vp.selectTool = false; }
        if (iconMenuItem(Icon::Scale, "Escalar", "T", !vp.selectTool && mode == GizmoMode::Scale)) { mode = GizmoMode::Scale; vp.selectTool = false; }
        if (iconMenuItem(Icon::Rotate, "Rotar", "R", !vp.selectTool && mode == GizmoMode::Rotate)) { mode = GizmoMode::Rotate; vp.selectTool = false; }
        ImGui::Separator();
        iconMenuItem(Icon::AxisX, "Eje X / Encabezado", "X", false, false);
        iconMenuItem(Icon::AxisY, "Eje Y / Inclinación", "Y", false, false);
        iconMenuItem(Icon::AxisZ, "Eje Z / Alabeo", "Z", false, false);
        if (g_vp && iconMenuItem(Icon::CoordSystem, "Sistema de Coordenadas de Mundo", "W", g_vp->worldCoords))
            g_vp->worldCoords = !g_vp->worldCoords;
        ImGui::Separator();
        ImGui::MenuItemEx("Medir y Construir", kIconGutterText, "", false, false);
        ImGui::MenuItemEx("Desplazamiento", kIconGutterText, "", false, false);
        ImGui::BeginMenuEx("Organizar Objetos", kIconGutterText, false);
        ImGui::BeginMenuEx("Centrar", kIconGutterText, false);
        ImGui::EndMenu();
    }

    auto stubTop = [](const char* name, std::initializer_list<std::pair<const char*, const char*>> items) {
        if (ImGui::BeginMenu(name)) {
            disabledItems(items);
            ImGui::EndMenu();
        }
    };
    if (ImGui::BeginMenu("Malla")) {
        if (iconMenuItem(Icon::MakeEditable, "Hacer Editable", "C", false, canMakeEditable(root, selected)))
            g.pendingCmd = UiCmd::MakeEditable;
        if (ImGui::MenuItem("Estado Actual a Objeto", nullptr, false, selected != nullptr)) g.pendingCmd = UiCmd::CurrentState;
        {
            bool gc = false;
            const Icon ic = Icon::ModConnect;
            if (gearMenuItem(&ic, "Conectar Objetos", nullptr, false, selected != nullptr, &gc)) g.pendingCmd = UiCmd::Connect;
            if (gc) openToolOptions((int)UiState::OptWin::Connect);
        }
        if (ImGui::MenuItem("Conectar Objetos + Eliminar", nullptr, false, selected != nullptr)) g.pendingCmd = UiCmd::ConnectDelete;
        disabledItems({{nullptr, nullptr},
                       {"Cuchillo", "K~K"}, {"Extrusión", "D"}, {"Extrusión Interna", "I"}, {"Bisel", "M~S"},
                       {"Puente", "M~B"}, {"Soldar", "M~Q"}, {"Pincel", "M~C"}, {"Planchar", "M~G"},
                       {nullptr, nullptr}, {"Invertir Normales", "U~R"}, {"Alinear Normales", "U~A"}});
        ImGui::EndMenu();
    }
    stubTop("Fijación", {{"Activar Fijación", "Mayús+S"}, {nullptr, nullptr}, {"Fijación 2D", ""},
                         {"Fijación 2.5D", ""}, {"Fijación 3D", ""}, {nullptr, nullptr}, {"Plano de Trabajo", ""}});

    if (ImGui::BeginMenu("Animación")) {
        if (iconMenuItem(Icon::Record, "Grabar Objetos Activos", "F9")) g.recordRequested = true;
        iconMenuItem(Icon::AutoKey, "Fotogramas Clave Automáticos", "Ctrl+F9", &g.autoKey);
        if (iconMenuItem(Icon::PrevKey, "Ir a la Clave Anterior", "Ctrl+F", false, !g.selKeys.empty())) jumpToKey(-1);
        if (iconMenuItem(Icon::NextKey, "Ir a la Clave Siguiente", "Ctrl+G", false, !g.selKeys.empty())) jumpToKey(1);
        if (ImGui::MenuItemEx("Eliminar Animación del Objeto", kIconGutterText, nullptr, false, !g.selKeys.empty()))
            g.pendingCmd = UiCmd::ClearAnimation;
        ImGui::Separator();
        if (iconMenuItem(Icon::Play, "Reproducir Hacia Delante", "F8", g.playDir == 1)) g.playDir = g.playDir == 1 ? 0 : 1;
        if (iconMenuItem(Icon::PlayBack, "Reproducir Hacia Atrás", "F6", g.playDir == -1)) g.playDir = g.playDir == -1 ? 0 : -1;
        if (iconMenuItem(Icon::Pause, "Parar", "F7")) g.playDir = 0;
        ImGui::Separator();
        if (iconMenuItem(Icon::ToStart, "Ir al Inicio", "Mayús+F")) g.frame = (float)g.minFrame;
        if (iconMenuItem(Icon::ToEnd, "Ir al Final", "Mayús+G")) g.frame = (float)g.maxFrame;
        if (iconMenuItem(Icon::StepFwd, "Fotograma Siguiente", "G")) g.frame = std::min((float)g.maxFrame, std::floor(g.frame) + 1);
        if (iconMenuItem(Icon::StepBack, "Fotograma Anterior", "F")) g.frame = std::max((float)g.minFrame, std::ceil(g.frame) - 1);
        ImGui::Separator();
        iconMenuItem(Icon::MnuLoop, "Repetir (Loop)", nullptr, &g.loop);
        ImGui::EndMenu();
    }
    stubTop("Simulación", {{"Partículas", ""}, {"Thinking Particles", ""}, {"Dinámicas", ""}, {"Cloth", ""},
                           {"Hair", ""}});
    if (ImGui::BeginMenu("Render")) {
        {
            bool g1 = false;
            const Icon iv = Icon::RenderView;
            if (gearMenuItem(&iv, "Renderizar Vista Activa", "Ctrl+R", false, true, &g1)) vp.renderView = true;
            if (g1) g.showRenderSettings = true;
        }
        iconMenuItem(Icon::MnuRenderRegion, "Renderizar Región", "Alt+R", false, false);
        ImGui::MenuItemEx("Renderizar Seleccionados en Visor de Imágenes", kIconGutterText, "", false, false);
        {
            bool g2 = false;
            const Icon ip = Icon::RenderPicture;
            if (gearMenuItem(&ip, "Renderizar en Visor de Imágenes", "Mayús+R", false, true, &g2)) g.renderRequested = true;
            if (g2) g.showRenderSettings = true;
        }
        iconMenuItem(Icon::MnuInteractiveRegion, "Región de Render Interactivo", "Alt+R", false, false);
        iconMenuItem(Icon::MnuMakePreview, "Crear Previsualización...", "Alt+B", false, false);
        ImGui::Separator();
        ImGui::MenuItemEx("Añadir a la Cola de Render...", kIconGutterText, "", false, false);
        iconMenuItem(Icon::MnuRenderQueue, "Cola de Render...", "", false, false);
        ImGui::Separator();
        if (iconMenuItem(Icon::RenderSettings, "Editar Configuración de Render...", "Ctrl+B")) g.showRenderSettings = true;
        if (iconMenuItem(Icon::MnuPictureViewer, "Visor de Imágenes...", "")) g.showPictureViewer = true;
        ImGui::EndMenu();
    }
    stubTop("Esculpir", {{"Subdividir", ""}, {"Reducir", ""}, {nullptr, nullptr}, {"Tirar", ""}, {"Suavizar", ""},
                         {"Inflar", ""}, {"Aplanar", ""}, {"Pellizcar", ""}, {"Amasar", ""}});
    stubTop("Motion Tracker", {{"Motion Tracker", ""}, {"Rastreador de Objetos", ""}});
    stubTop("MoGraph", {{"Ocultar Seleccionado", ""}, {"Intercambiar Clonador-Matriz", ""}, {"Selección MoGraph", ""},
                        {"Pincel Pesos MoGraph", ""}, {nullptr, nullptr}, {"Clonador", ""}, {"Matriz", ""},
                        {"Fractura", ""}, {"Fractura Voronoi", ""}, {"MoText", ""}, {"PolyFX", ""}});
    stubTop("Character", {{"Junta", ""}, {"Herramienta de Junta", ""}, {"Pintar Pesos", ""}, {"Etiqueta IK", ""},
                          {"Construcción de Personaje", ""}});
    stubTop("Línea de Trabajo", {{"Sketch and Toon", ""}, {"Hair", ""}});
    stubTop("Plugins", {{"Ningún plugin cargado", ""}, {nullptr, nullptr}, {"Buscar Plugins...", ""}});
    stubTop("Script", {{"Gestor de Scripts...", ""}, {"Consola...", "Mayús+F10"}, {nullptr, nullptr},
                       {"Ejecutar Script...", ""}});

    if (ImGui::BeginMenu("Ventana")) {
        // Los nombres siguen al preset (Esquema / Outliner / Scene Explorer...).
        const PanelNames& n = names();
        if (iconMenuItem(Icon::MnuObjManager, n.objects, "Mayús+F1", !g.browserOpen)) g.browserOpen = false;
        iconMenuItem(Icon::MnuMatManager, n.materials, "Mayús+F2", true, false);
        iconMenuItem(Icon::MnuTimeline, n.timeline, "Mayús+F3", true, false);
        iconMenuItem(Icon::MnuAttributes, n.attributes, "Mayús+F5", true, false);
        iconMenuItem(Icon::MnuCoords, n.coords, "Mayús+F7", true, false);
        ImGui::Separator();
        if (iconMenuItem(Icon::MnuMatEditor, "Editor de Material...", nullptr, g.showMatEditor)) g.showMatEditor = !g.showMatEditor;
        if (iconMenuItem(Icon::MnuContentBrowser, "Navegador de Contenido", "Mayús+F8", g.browserOpen))
            g.browserOpen = !g.browserOpen;
        iconMenuItem(Icon::MnuLayers, "Gestor de Capas", "Mayús+F4", false, false);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Ayuda")) {
        iconMenuItem(Icon::MnuHelp, "Mostrar Ayuda...", "F1", false, false);
        ImGui::Separator();
        if (iconMenuItem(Icon::MnuAbout, "Acerca de Iron3D Studio...")) g.showAbout = true;
        ImGui::EndMenu();
    }

    // Selector de "Entorno" a la derecha (seccion 13): de momento solo el de
    // Arranque existe en Iron3D. La etiqueta sigue al preset ("Espacio de
    // trabajo:" en Blender, "Workspace:" en Maya...).
    static const char* kLayouts[] = {"Arranque", "Animar", "Modelado", "BP - UV Edit", "Esculpir", "Visualizar"};
    const char* layoutLabel = names().layoutLabel;
    const float comboW = 140.0f;
    const float labelW = ImGui::CalcTextSize(layoutLabel).x;
    ImGui::SameLine(ImGui::GetWindowWidth() - comboW - labelW - 24.0f);
    ImGui::TextUnformatted(layoutLabel);
    ImGui::SetNextItemWidth(comboW);
    if (ImGui::BeginCombo("##layout", kLayouts[g.layoutPreset])) {
        for (int i = 0; i < IM_ARRAYSIZE(kLayouts); ++i) {
            if (i > 0) ImGui::BeginDisabled();
            if (ImGui::Selectable(kLayouts[i], g.layoutPreset == i)) g.layoutPreset = i;
            if (i > 0) ImGui::EndDisabled();
        }
        ImGui::EndCombo();
    }
    (void)camera;
    ImGui::EndMainMenuBar();
}

// --- Barra de iconos superior (seccion 3) -------------------------------------------

void drawTopToolbar(SceneNode& root, SceneNode*& selected, GizmoMode& mode, SceneHistory& history,
                    ViewportState& vp, ImVec2 pos, ImVec2 size) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6, 3));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(2, 0));
    beginPanel("##toptoolbar", pos, size, ImGuiWindowFlags_NoScrollbar);
    const float b = size.y - 6.0f;

    const bool couldUndo = history.canUndo();
    if (iconButton("undo", Icon::Undo, b, false, couldUndo, "Deshacer [Ctrl+Z]", "Deshacer la última acción.") == BtnResult::Click)
        if (history.undo(root)) selected = nullptr;
    ImGui::SameLine();
    const bool couldRedo = history.canRedo();
    if (iconButton("redo", Icon::Redo, b, false, couldRedo, "Rehacer [Ctrl+Y]", "Rehacer la última acción deshecha.") == BtnResult::Click)
        if (history.redo(root)) selected = nullptr;
    ImGui::SameLine();
    toolbarSeparator(true, b);
    ImGui::SameLine();

    if (iconButton("live", Icon::LiveSelect, b, vp.selectTool, true, "Selección en Vivo [Atajo de Teclado 9]",
                   "Selección en Vivo: Haga clic para seleccionar elementos. Pulse MAYÚSCULAS para añadir a la selección, CTRL para eliminar.") == BtnResult::Click) {
        vp.selectTool = true;
        vp.selectShape = 0;
    }
    ImGui::SameLine();
    struct ToolDef { const char* id; Icon icon; GizmoMode m; const char* tip; const char* hint; };
    const ToolDef tools[] = {
        {"move", Icon::Move, GizmoMode::Translate, "Herramienta de Mover [Atajo de Teclado E]",
         "Mover: Haga clic y arrastre para mover elementos. Pulse MAYÚSCULAS para cuantificar el movimiento / añadir a la selección en el modo punto, CTRL para eliminar."},
        {"scale", Icon::Scale, GizmoMode::Scale, "Herramienta de Escalar [Atajo de Teclado T]",
         "Escalar: Haga clic y arrastre para escalar elementos. Pulse MAYÚSCULAS para cuantificar el escalado / añadir a la selección en el modo punto, CTRL para eliminar."},
        {"rotate", Icon::Rotate, GizmoMode::Rotate, "Herramienta de Rotar [Atajo de Teclado R]",
         "Rotar: Haga clic y arrastre para rotar elementos. Pulse MAYÚSCULAS para cuantificar la rotación / añadir a la selección en el modo punto, CTRL para eliminar."},
    };
    for (const auto& t : tools) {
        if (iconButton(t.id, t.icon, b, !vp.selectTool && mode == t.m, true, t.tip, t.hint) == BtnResult::Click) {
            mode = t.m;
            vp.selectTool = false;
        }
        ImGui::SameLine();
    }
    toolbarSeparator(true, b);
    ImGui::SameLine();

    // Bloqueo de ejes y sistema de coordenadas: activos por defecto en C4D,
    // todavia sin efecto en el gizmo de Iron3D.
    iconButton("ax", Icon::AxisX, b, true, false, "Eje X / Encabezado [X]\n(todavía no implementado)", "Bloqueo de eje X: todavía no implementado.");
    ImGui::SameLine();
    iconButton("ay", Icon::AxisY, b, true, false, "Eje Y / Inclinación [Y]\n(todavía no implementado)", "Bloqueo de eje Y: todavía no implementado.");
    ImGui::SameLine();
    iconButton("az", Icon::AxisZ, b, true, false, "Eje Z / Alabeo [Z]\n(todavía no implementado)", "Bloqueo de eje Z: todavía no implementado.");
    ImGui::SameLine();
    if (iconButton("coord", Icon::CoordSystem, b, vp.worldCoords, true,
                   vp.worldCoords ? "Usar Sistema de Coordenadas de Objeto [W]\n(ahora: Mundo)"
                                  : "Usar Sistema de Coordenadas de Mundo [W]\n(ahora: Objeto)",
                   "Cambia los ejes del gizmo entre los del objeto y los del mundo.") == BtnResult::Click)
        vp.worldCoords = !vp.worldCoords;
    ImGui::SameLine();
    toolbarSeparator(true, b);
    ImGui::SameLine();

    if (iconButton("rview", Icon::RenderView, b, false, true,
                   "Renderizar Vista Activa\nRenderiza la vista activa en el propio visor.\n[Atajo de Teclado Ctrl+R]",
                   "Renderiza la vista activa en el propio visor.") == BtnResult::Click)
        vp.renderView = true;
    ImGui::SameLine();
    if (iconButton("rpv", Icon::RenderPicture, b, false, true,
                   "Renderizar en Visor de Imágenes [Mayús+R]\nRenderiza la cámara activa con la Configuración de Render.",
                   "Renderiza la cámara activa en el Visor de Imágenes.") == BtnResult::Click)
        g.renderRequested = true;
    ImGui::SameLine();
    if (iconButton("rset", Icon::RenderSettings, b, g.showRenderSettings, true, "Editar Configuración de Render... [Ctrl+B]",
                   "Resolución, antialiasing y fondo del render.") == BtnResult::Click)
        g.showRenderSettings = !g.showRenderSettings;
    ImGui::SameLine();
    toolbarSeparator(true, b);
    ImGui::SameLine();

    struct GroupDef { const char* id; Icon icon; const char* popup; const PaletteItem* items; int count; int cols;
                      PrimitiveType defType; bool defOk; const char* tip; };
    const GroupDef groups[] = {
        {"gcube", Icon::Cube, "##palPrim", kPrimitives, countOf(kPrimitives), 4, PrimitiveType::Cube, true,
         "Cubo  (mantener pulsado: más objetos)"},
        {"gspline", Icon::Spline, "##palSpline", kSplines, countOf(kSplines), 3, PrimitiveType::None, false,
         "Lápiz  (mantener pulsado: más splines)"},
        {"gnurbs", Icon::Nurbs, "##palGen", kGenerators, countOf(kGenerators), 2, PrimitiveType::None, false,
         "Subdivisión Superficie  (mantener pulsado: generadores)"},
        {"gmodel", Icon::ModArray, "##palModel", kModeling, countOf(kModeling), 2, PrimitiveType::None, false,
         "Matriz  (mantener pulsado: modelado)"},
        {"gdeform", Icon::Deformer, "##palDef", kDeformers, countOf(kDeformers), 4, PrimitiveType::None, false,
         "Doblar  (mantener pulsado: deformadores)"},
        {"gscene", Icon::Floor, "##palScene", kScene, countOf(kScene), 3, PrimitiveType::None, false,
         "Suelo  (mantener pulsado: escena)"},
        {"gcam", Icon::Camera, "##palCam", kCameras, countOf(kCameras), 1, PrimitiveType::None, false,
         "Cámara  (mantener pulsado: más cámaras)"},
        {"glight", Icon::Light, "##palLight", kLights, countOf(kLights), 3, PrimitiveType::Light, true,
         "Luz  (mantener pulsado: más luces)"},
    };
    for (const auto& gr : groups) {
        const BtnResult r = iconButton(gr.id, gr.icon, b, false, true, gr.tip,
                                       "Clic: crear el objeto. Mantener pulsado o clic derecho: abrir la paleta del grupo. Alt+Clic: crear como hijo del seleccionado.",
                                       true);
        const ImVec2 under(ImGui::GetItemRectMin().x, ImGui::GetItemRectMax().y + 2);
        if (r == BtnResult::Click) {
            if (gr.defOk) createObject(root, selected, history, gr.defType);
            else ImGui::OpenPopup(gr.popup);
        }
        if (r == BtnResult::Palette) ImGui::OpenPopup(gr.popup);
        ImGui::SetNextWindowPos(under, ImGuiCond_Appearing);
        drawPalette(gr.popup, gr.items, gr.count, gr.cols, root, selected, history);
        ImGui::SameLine();
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
}

// --- Barra de modos izquierda (seccion 4) ------------------------------------------

void drawLeftToolbar(ImVec2 pos, ImVec2 size) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(3, 4));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 2));
    beginPanel("##lefttoolbar", pos, size, ImGuiWindowFlags_NoScrollbar);
    const float b = size.x - 6.0f;
    if (iconButton("mkedit", Icon::MakeEditable, b, false, true,
                   "Hacer Editable [C]\nConvierte Cubo, Esfera o Plano en un Objeto Poligonal.",
                   "Hacer Editable: convierte los objetos paramétricos seleccionados en Objetos Poligonales.") ==
        BtnResult::Click)
        g.pendingCmd = UiCmd::MakeEditable;
    toolbarSeparator(false, b);
    const int cm = g_vp ? g_vp->compMode : 0;
    if (iconButton("model", Icon::ModelMode, b, cm == 0, true, "Usar Modo Modelo", "Modo Modelo: se editan objetos enteros.") ==
            BtnResult::Click && g_vp)
        g_vp->compMode = 0;
    iconButton("texmode", Icon::TextureMode, b, false, false, "Usar Modo Textura\n(todavía no implementado)", "Modo Textura: todavía no implementado.");
    if (iconButton("axmode", Icon::AxisMode, b, g_vp && g_vp->axisMode, g_vp != nullptr, "Activar Eje [L]",
                   "Modo Eje: mover, rotar o escalar cambia solo el eje del objeto; la geometría editable y los hijos se quedan en su sitio.") ==
            BtnResult::Click &&
        g_vp)
        g_vp->axisMode = !g_vp->axisMode;
    toolbarSeparator(false, b);
    if (iconButton("pts", Icon::Points, b, cm == 1, true, "Usar Modo Puntos",
                   "Modo Puntos: elige y mueve puntos de un objeto editable (C para hacerlo editable).") == BtnResult::Click && g_vp)
        g_vp->compMode = 1;
    if (iconButton("edg", Icon::Edges, b, cm == 2, true, "Usar Modo Aristas",
                   "Modo Aristas: elige y mueve aristas de un objeto editable.") == BtnResult::Click && g_vp)
        g_vp->compMode = 2;
    if (iconButton("pol", Icon::Polygons, b, cm == 3, true, "Usar Modo Polígonos",
                   "Modo Polígonos: elige y mueve polígonos (triángulos) de un objeto editable.") == BtnResult::Click && g_vp)
        g_vp->compMode = 3;
    toolbarSeparator(false, b);
    iconButton("tweak", Icon::Tweak, b, false, false, "Modo Ajuste\n(todavía no implementado)", "Modo Ajuste: todavía no implementado.");
    iconButton("solo", Icon::Solo, b, false, false, "Vista Solo\n(todavía no implementado)", "Vista Solo: todavía no implementado.");
    toolbarSeparator(false, b);
    iconButton("snap", Icon::Snap, b, false, false, "Activar Fijación [Mayús+S]\n(todavía no implementado)", "Fijación: todavía no implementado.");
    iconButton("wplane", Icon::Workplane, b, false, false, "Plano de Trabajo\n(todavía no implementado)", "Plano de Trabajo: todavía no implementado.");
    iconButton("wlock", Icon::Locked, b, false, false, "Bloquear Plano de Trabajo\n(todavía no implementado)", "Bloquear Plano de Trabajo: todavía no implementado.");
    ImGui::End();
    ImGui::PopStyleVar(2);
}

// --- Visor: menu propio + overlays (secciones 11, 12 y 16) --------------------------

void drawViewportHeader(SceneNode& root, SceneNode*& selected, OrbitalCamera& camera, ViewportState& vp,
                        ImVec2 pos, ImVec2 size) {
    beginPanel("##vpheader", pos, size, ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoScrollbar);
    if (ImGui::BeginMenuBar()) {
        if (ImGui::BeginMenu("Vista")) {
            ImGui::MenuItem("Usar como Cámara de Render", nullptr, false, false);
            ImGui::Separator();
            disabledItems({{"Deshacer Vista", "Ctrl+Mayús+Z"}, {"Rehacer Vista", "Ctrl+Mayús+Y"}, {nullptr, nullptr}});
            if (ImGui::MenuItem("Encuadrar Todo", "Alt+H")) frameNodes(camera, root, false);
            if (ImGui::MenuItem("Encuadrar Geometría", "H")) frameNodes(camera, root, false);
            if (ImGui::MenuItem("Encuadrar Elementos Activos", "O", false, selected != nullptr))
                frameNodes(camera, *selected, true);
            if (ImGui::MenuItem("Encuadre por Defecto", "Mayús+H")) camera.resetView();
            ImGui::Separator();
            ImGui::MenuItem("Redibujar", "A", false, false);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Cámaras")) {
            ImGui::BeginMenu("Navegación", false);
            if (ImGui::BeginMenu("Usar Cámara")) {
                ImGui::MenuItem("Cámara del Editor", nullptr, true);
                ImGui::EndMenu();
            }
            ImGui::Separator();
            using CV = OrbitalCamera::View;
            const CV cv = camera.view();
            if (ImGui::MenuItem("Perspectiva", nullptr, cv == CV::Perspective)) selectView(camera, CV::Perspective);
            ImGui::MenuItem("Paralela", nullptr, false, false);
            if (ImGui::MenuItem("Izquierda", nullptr, cv == CV::Left)) selectView(camera, CV::Left);
            if (ImGui::MenuItem("Derecha", nullptr, cv == CV::Right)) selectView(camera, CV::Right);
            if (ImGui::MenuItem("Frontal", nullptr, cv == CV::Front)) selectView(camera, CV::Front);
            if (ImGui::MenuItem("Trasera", nullptr, cv == CV::Back)) selectView(camera, CV::Back);
            if (ImGui::MenuItem("Superior", nullptr, cv == CV::Top)) selectView(camera, CV::Top);
            if (ImGui::MenuItem("Inferior", nullptr, cv == CV::Bottom)) selectView(camera, CV::Bottom);
            disabledItems({{nullptr, nullptr}, {"Axonométrica", ""}});
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Mostrar")) {
            ImGui::MenuItem("Sombreado Gouraud", "N~A", true);
            disabledItems({{"Sombreado Gouraud (Líneas)", "N~B"}, {"Sombreado Rápido", "N~C"},
                           {"Sombreado Rápido (Líneas)", "N~D"}, {"Sombreado Constante", "N~E"},
                           {"Sombreado Constante (Líneas)", "N~F"}, {"Líneas Ocultas", "N~G"}, {"Líneas", "N~H"},
                           {nullptr, nullptr}, {"Alambre", "N~I"}, {"Isoparms", "N~J"}, {"Caja", "N~K"},
                           {"Esqueleto", "N~L"}});
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Opciones")) {
            ImGui::BeginMenu("Nivel de Detalle", false);
            disabledItems({{nullptr, nullptr}, {"Estereoscópico", ""}, {"Sombreado de Flujo de Trabajo Lineal", ""},
                           {nullptr, nullptr}, {"OpenGL Mejorado", ""}, {"Ruidos", ""}, {"Post Efectos", ""},
                           {"Sombras", ""}, {"Transparencia", ""}, {"Reflexiones", ""}, {"SSAO", ""},
                           {"Teselación", ""}, {"Profundidad de Campo", ""}, {nullptr, nullptr},
                           {"Ocultar Caras Posteriores", "N~P"}, {"Usar Edición de Isolíneas", "Alt+A"},
                           {"Color de Capa", ""}, {"Normales de Polígonos", ""}, {"Normales de Vértices", ""},
                           {nullptr, nullptr}, {"Etiquetas", "N~O"}, {"Texturas", "N~Q"}, {"Rayos X", "N~R"},
                           {nullptr, nullptr}, {"Luz por Defecto...", ""}, {"Configurar...", "Mayús+V"},
                           {"Configurar Todo...", "Alt+V"}});
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Filtrar")) {
            ImGui::MenuItem("Cuadrícula", nullptr, &vp.showGrid);
            ImGui::Separator();
            // El resto de filtros todavia no tienen efecto: se ven activos
            // (como en C4D por defecto) pero no se pueden cambiar.
            for (const char* f : {"Nulo", "Polígono", "Spline", "Generador", "Subdivisión Superficie", "Deformador",
                                  "Cámara", "Luz", "Entorno", "Partícula", "Otro"})
                ImGui::MenuItem(f, nullptr, true, false);
            ImGui::Separator();
            for (const char* f : {"Horizonte", "Ejes Globales", "Cajas Delimitadoras", "HUD", "Resaltar Componente",
                                  "Ejes", "Guías", "Resaltar Objeto", "Cruz Navegación"})
                ImGui::MenuItem(f, nullptr, true, false);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Paneles")) {
            if (ImGui::BeginMenu("Disposición")) {
                if (ImGui::MenuItem("Vista Simple", nullptr, !vp.quad) && vp.quad)
                    exitQuad(camera, vp, camera.view());
                disabledItems({{"2 Vistas Apiladas", ""}, {"2 Vistas Lado a Lado", ""}});
                if (ImGui::MenuItem("4 Vistas", nullptr, vp.quad) && !vp.quad) enterQuad(camera, vp);
                ImGui::EndMenu();
            }
            ImGui::Separator();
            {
                using CV = OrbitalCamera::View;
                const CV cv = camera.view();
                if (ImGui::MenuItem("Vista 1 (Perspectiva)", "F1", cv == CV::Perspective)) selectView(camera, CV::Perspective);
                if (ImGui::MenuItem("Vista 2 (Superior)", "F2", cv == CV::Top)) camera.setView(CV::Top);
                if (ImGui::MenuItem("Vista 3 (Derecha)", "F3", cv == CV::Right)) camera.setView(CV::Right);
                if (ImGui::MenuItem("Vista 4 (Frontal)", "F4", cv == CV::Front)) selectView(camera, CV::Front);
                if (ImGui::MenuItem("Todas las Vistas", "F5", vp.quad)) toggleQuad(camera);
            }
            ImGui::EndMenu();
        }
        ImGui::BeginMenu("ProRender", false);

        // Iconos de navegacion de la esquina: arrastrar sobre ellos mueve la
        // camara igual que Alt+clic (como en C4D).
        const float ib = ImGui::GetFrameHeight() - 2.0f;
        ImGui::SameLine(ImGui::GetWindowWidth() - 4 * (ib + 3) - 10);
        ImGuiIO& io = ImGui::GetIO();
        struct NavDef { const char* id; Icon icon; int op; const char* tip; };
        const NavDef navs[] = {
            {"npan", Icon::NavPan, 0, "Mover Cámara (arrastrar)"},
            {"nzoom", Icon::NavZoom, 1, "Zoom de Cámara (arrastrar)"},
            {"nrot", Icon::NavRotate, 2, "Rotar Cámara (arrastrar)"},
            {"nlay", Icon::NavLayout, 3, "Cambiar Vista (todavía no implementado)"},
        };
        for (const auto& n : navs) {
            iconButton(n.id, n.icon, ib, false, n.op != 3, n.tip, n.op == 3 ? "Multi-vista: todavía no implementado." : "Arrastre sobre el icono para navegar por la vista.");
            if (ImGui::IsItemActive() && n.op != 3) {
                if (n.op == 0) camera.pan(io.MouseDelta.x, io.MouseDelta.y);
                if (n.op == 1) camera.dolly(io.MouseDelta.y - io.MouseDelta.x);
                if (n.op == 2) camera.orbit(io.MouseDelta.x, io.MouseDelta.y);
            }
            ImGui::SameLine(0, 3);
        }
        ImGui::EndMenuBar();
    }
    ImGui::End();
}

// Contorno naranja del objeto seleccionado (como en C4D: solo la silueta, sin
// aristas interiores). Las primitivas son convexas, asi que la silueta es la
// envolvente convexa de sus puntos proyectados a pantalla.
bool nodeEditorVisible(const SceneNode* n) {
    std::vector<const SceneNode*> chain;
    for (; n; n = n->parent()) chain.push_back(n);
    bool vis = true;
    for (auto it = chain.rbegin(); it != chain.rend(); ++it) vis = resolveVisibility((*it)->editorVis, vis);
    return vis;
}

void drawSelectionOutline(ImDrawList* dl, const SceneNode* sel, const OrbitalCamera& camera, const ViewportState& vp) {
    if (!sel || vp.w < 2.0f || vp.h < 2.0f || !nodeEditorVisible(sel)) return;
    std::vector<glm::vec3> local;
    switch (sel->primitive) {
        case PrimitiveType::Cube:
            for (int i = 0; i < 8; ++i)
                local.emplace_back(i & 1 ? 0.5f : -0.5f, i & 2 ? 0.5f : -0.5f, i & 4 ? 0.5f : -0.5f);
            break;
        case PrimitiveType::Plane:
            for (int i = 0; i < 4; ++i) local.emplace_back(i & 1 ? 0.5f : -0.5f, 0.0f, i & 2 ? 0.5f : -0.5f);
            break;
        case PrimitiveType::Sphere:
            for (int la = 0; la <= 16; ++la) {
                const float th = 3.14159265f * la / 16.0f;
                for (int lo = 0; lo < 32; ++lo) {
                    const float ph = 6.2831853f * lo / 32.0f;
                    local.emplace_back(0.5f * std::sin(th) * std::cos(ph), 0.5f * std::cos(th),
                                       0.5f * std::sin(th) * std::sin(ph));
                }
            }
            break;
        case PrimitiveType::Mesh: {
            if (!sel->mesh) return;
            const glm::vec3 lo = sel->mesh->boundsMin, hi = sel->mesh->boundsMax;
            for (int i = 0; i < 8; ++i)
                local.emplace_back(i & 1 ? hi.x : lo.x, i & 2 ? hi.y : lo.y, i & 4 ? hi.z : lo.z);
            break;
        }
        default:
            return;
    }
    const glm::mat4 mvp = camera.projectionMatrix(vp.w / vp.h, false) * camera.viewMatrix() * sel->worldMatrix();
    std::vector<ImVec2> pts;
    pts.reserve(local.size());
    for (const auto& l : local) {
        const glm::vec4 c = mvp * glm::vec4(l, 1.0f);
        if (c.w <= 0.06f) return; // parte detras de la camara: sin contorno fiable
        pts.emplace_back(vp.x + (c.x / c.w * 0.5f + 0.5f) * vp.w, vp.y + (0.5f - c.y / c.w * 0.5f) * vp.h);
    }
    // Envolvente convexa (cadena monotona de Andrew).
    std::sort(pts.begin(), pts.end(), [](const ImVec2& l, const ImVec2& r) { return l.x < r.x || (l.x == r.x && l.y < r.y); });
    auto cross = [](const ImVec2& o, const ImVec2& p, const ImVec2& q) {
        return (p.x - o.x) * (q.y - o.y) - (p.y - o.y) * (q.x - o.x);
    };
    std::vector<ImVec2> hull(pts.size() * 2);
    size_t k = 0;
    for (size_t i = 0; i < pts.size(); ++i) {
        while (k >= 2 && cross(hull[k - 2], hull[k - 1], pts[i]) <= 0) --k;
        hull[k++] = pts[i];
    }
    for (size_t i = pts.size() - 1, t = k + 1; i > 0; --i) {
        while (k >= t && cross(hull[k - 2], hull[k - 1], pts[i - 1]) <= 0) --k;
        hull[k++] = pts[i - 1];
    }
    if (k < 3) return;
    dl->AddPolyline(hull.data(), (int)k - 1, kSelOrange, ImDrawFlags_Closed, 1.5f);
}

// Contorna todos los objetos de la seleccion, no solo el activo.
void outlineSelected(ImDrawList* dl, const SceneNode& node, const OrbitalCamera& camera, const ViewportState& vp) {
    for (const auto& c : node.children()) {
        if (g.selIds.count(c->id())) drawSelectionOutline(dl, c.get(), camera, vp);
        outlineSelected(dl, *c, camera, vp);
    }
}

void drawViewportOverlay(const OrbitalCamera& camera, const ViewportState& vp, const SceneNode& root);

// Vista de 4: un clic (cualquier boton) en otro panel lo activa -- su camara
// pasa a ser la `camera` de siempre --, se dibujan los overlays de los tres
// inactivos y x/y/w/h quedan en el panel activo para todo lo demas.
void drawQuadPanels(OrbitalCamera& camera, ViewportState& vp, const SceneNode& root) {
    const ImGuiIO& io = ImGui::GetIO();
    const bool click = ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right) ||
                       ImGui::IsMouseClicked(ImGuiMouseButton_Middle);
    if (click && !io.WantCaptureMouse) {
        for (int i = 0; i < 4; ++i) {
            const QuadRect r = quadPanelRect(vp, i);
            if (i == vp.quadActive || io.MousePos.x < r.x || io.MousePos.y < r.y || io.MousePos.x >= r.x + r.w ||
                io.MousePos.y >= r.y + r.h)
                continue;
            vp.quadCams[vp.quadActive] = camera;
            camera = vp.quadCams[i];
            camera.resetMouseDelta(io.MousePos.x, io.MousePos.y);
            vp.quadActive = i;
            break;
        }
    }
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    for (int i = 0; i < 4; ++i) {
        if (i == vp.quadActive) continue;
        const QuadRect r = quadPanelRect(vp, i);
        ViewportState pv = vp;
        pv.x = r.x; pv.y = r.y; pv.w = r.w; pv.h = r.h;
        drawViewportOverlay(vp.quadCams[i], pv, root);
    }
    const QuadRect a = quadPanelRect(vp, vp.quadActive);
    vp.x = a.x; vp.y = a.y; vp.w = a.w; vp.h = a.h;
    // Marco claro alrededor del panel activo (en C4D el activo se resalta).
    dl->AddRect(ImVec2(a.x - 1, a.y - 1), ImVec2(a.x + a.w + 1, a.y + a.h + 1), IM_COL32(200, 200, 200, 255));
}

void drawViewportOverlay(const OrbitalCamera& camera, const ViewportState& vp, const SceneNode& root) {
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    const ImVec2 a(vp.x, vp.y), b(vp.x + vp.w, vp.y + vp.h);
    dl->PushClipRect(a, b, true);
    if (!vp.renderView) outlineSelected(dl, root, camera, vp);
    const char* viewName = viewLabel(camera.view(), names().viewport);
    dl->AddText(ImVec2(a.x + 8, a.y + 5), IM_COL32(230, 230, 230, 255), viewName);

    // Triedro de ejes (esquina inf-izq, seccion 12): rotacion de la vista sin traslacion.
    const glm::mat3 rot(camera.viewMatrix());
    const ImVec2 o(a.x + 42, b.y - 38);
    struct Ax { glm::vec3 d; ImU32 col; const char* lbl; };
    Ax axes[3] = {{rot * glm::vec3(1, 0, 0), IM_COL32(225, 60, 55, 255), "X"},
                  {rot * glm::vec3(0, 1, 0), IM_COL32(80, 190, 70, 255), "Y"},
                  {rot * glm::vec3(0, 0, 1), IM_COL32(70, 110, 230, 255), "Z"}};
    std::sort(std::begin(axes), std::end(axes), [](const Ax& l, const Ax& r) { return l.d.z < r.d.z; });
    for (const auto& ax : axes) {
        const ImVec2 e(o.x + ax.d.x * 26.0f, o.y - ax.d.y * 26.0f);
        dl->AddLine(o, e, ax.col, 2.0f);
        dl->AddText(ImVec2(e.x + (ax.d.x >= 0 ? 3 : -11), e.y - 8), ax.col, ax.lbl);
    }

    const char* gridTxt = "Espaciado Cuadrícula : 100 cm";
    const ImVec2 ts = ImGui::CalcTextSize(gridTxt);
    if (vp.showGrid && !vp.renderView)
        dl->AddText(ImVec2(b.x - ts.x - 10, b.y - ts.y - 6), IM_COL32(220, 220, 220, 255), gridTxt);
    dl->PopClipRect();
    dl->AddRect(ImVec2(a.x - 1, a.y - 1), ImVec2(b.x + 1, b.y + 1), kBorderDark);
}

// --- Flecha de objeto fuera de vista --------------------------------------------------
// Como en C4D: si el objeto activo queda fuera del visor, detras de la camara o
// tan lejos que no se ve, sale una flechita naranja apuntando hacia el. Clic =
// la camara viaja hasta encuadrarlo (sin cambiar el angulo de vista).

constexpr float kCamTripSeconds = 0.35f;
constexpr float kFarPlane = 100.0f; // el de OrbitalCamera::projectionMatrix

// Centro y radio de la esfera envolvente del nodo (con sus hijos). Un nulo sin
// geometria cuenta como un punto en su posicion.
void nodeSphere(const SceneNode& node, glm::vec3& center, float& radius) {
    glm::vec3 mn(0.0f), mx(0.0f);
    bool any = false;
    accumulateBounds(node, mn, mx, any);
    if (any) {
        center = (mn + mx) * 0.5f;
        radius = glm::length(mx - mn) * 0.5f;
    } else {
        center = glm::vec3(node.worldMatrix()[3]);
        radius = 0.0f;
    }
}

void startCamTrip(OrbitalCamera& camera, const SceneNode& node) {
    glm::vec3 center;
    float radius;
    nodeSphere(node, center, radius);
    auto& t = g.camTrip;
    t.active = true;
    t.t = 0.0f;
    t.fromTarget = camera.target();
    t.fromDist = camera.distance();
    t.toTarget = center;
    t.toDist = OrbitalCamera::frameDistance(std::max(0.25f, radius));
    t.lastTarget = t.fromTarget;
    t.lastDist = t.fromDist;
}

void tickCamTrip(OrbitalCamera& camera) {
    auto& t = g.camTrip;
    if (!t.active) return;
    // Otra cosa ha movido la camara (Alt+clic, H...): manda el usuario.
    if (glm::length(camera.target() - t.lastTarget) > 1e-4f || std::abs(camera.distance() - t.lastDist) > 1e-4f) {
        t.active = false;
        return;
    }
    t.t = std::min(1.0f, t.t + ImGui::GetIO().DeltaTime / kCamTripSeconds);
    const float s = t.t * t.t * (3.0f - 2.0f * t.t); // smoothstep: arranca y frena suave
    camera.setTargetDistance(glm::mix(t.fromTarget, t.toTarget, s), t.fromDist + (t.toDist - t.fromDist) * s);
    t.lastTarget = camera.target();
    t.lastDist = camera.distance();
    if (t.t >= 1.0f) t.active = false;
}

// Donde y hacia donde va la flecha. false = el objeto se ve bien, sin flecha.
// `tip` es la punta; `dir`, la direccion (unitaria, en pixeles) hacia el objeto.
bool offscreenArrow(const OrbitalCamera& camera, const ViewportState& vp, const SceneNode& node, ImVec2& tip,
                    ImVec2& dir) {
    glm::vec3 center;
    float radius;
    nodeSphere(node, center, radius);
    const glm::vec3 pv = glm::vec3(camera.viewMatrix() * glm::vec4(center, 1.0f));
    const float depth = -pv.z; // > 0: delante de la camara
    const ImVec2 mid(vp.x + vp.w * 0.5f, vp.y + vp.h * 0.5f);
    const float margin = 3.0f; // la punta toca el borde del visor, como en C4D
    auto normalize = [](ImVec2 v) {
        const float l = std::sqrt(v.x * v.x + v.y * v.y);
        return l > 1e-4f ? ImVec2(v.x / l, v.y / l) : ImVec2(0.0f, 1.0f);
    };
    auto toEdge = [&](ImVec2 d) {
        const float hw = vp.w * 0.5f - margin, hh = vp.h * 0.5f - margin;
        const float tx = std::abs(d.x) > 1e-4f ? hw / std::abs(d.x) : 1e9f;
        const float ty = std::abs(d.y) > 1e-4f ? hh / std::abs(d.y) : 1e9f;
        const float k = std::min(tx, ty);
        return ImVec2(mid.x + d.x * k, mid.y + d.y * k);
    };

    if (depth + radius <= 0.05f) { // entero detras de la camara
        dir = normalize(ImVec2(pv.x, -pv.y));
        tip = toEdge(dir);
        return true;
    }
    if (depth <= 0.05f) return false; // la camara esta dentro o casi: se ve

    const glm::mat4 proj = camera.projectionMatrix(vp.w / vp.h, false);
    const glm::vec4 c = proj * glm::vec4(pv, 1.0f);
    const ImVec2 sp(vp.x + (c.x / c.w * 0.5f + 0.5f) * vp.w, vp.y + (0.5f - c.y / c.w * 0.5f) * vp.h);
    const float rpx = radius / depth * proj[1][1] * vp.h * 0.5f;
    const bool outside = sp.x + rpx < vp.x || sp.x - rpx > vp.x + vp.w || sp.y + rpx < vp.y ||
                         sp.y - rpx > vp.y + vp.h;
    if (outside) {
        dir = normalize(ImVec2(sp.x - mid.x, sp.y - mid.y));
        tip = toEdge(dir);
        return true;
    }
    // Dentro del encuadre pero perdido: mas alla del plano lejano (no se
    // dibuja) o reducido a un par de pixeles. La flecha se pone a su lado.
    if (depth - radius > kFarPlane || rpx < 3.0f) {
        ImVec2 d(sp.x - mid.x, sp.y - mid.y);
        if (d.x * d.x + d.y * d.y < 30.0f * 30.0f) d = ImVec2(1.0f, 1.0f);
        dir = normalize(d);
        tip = ImVec2(sp.x - dir.x * (rpx + 5.0f), sp.y - dir.y * (rpx + 5.0f));
        return true;
    }
    return false;
}

void drawOffscreenArrow(OrbitalCamera& camera, const ViewportState& vp, SceneNode* sel) {
    if (!sel || vp.renderView || vp.w < 60.0f || vp.h < 60.0f || !nodeEditorVisible(sel)) return;
    ImVec2 tip, d;
    if (!offscreenArrow(camera, vp, *sel, tip, d)) return;

    constexpr float len = 17.0f, half = 6.0f, hit = 28.0f;
    const ImVec2 base(tip.x - d.x * len, tip.y - d.y * len);
    const ImVec2 perp(-d.y, d.x);
    const ImVec2 hc(tip.x - d.x * len * 0.5f, tip.y - d.y * len * 0.5f);

    // Ventana invisible encima de la flecha: asi ImGui se queda el clic y el
    // visor no lo toma como seleccion/deseleccion.
    ImGui::SetNextWindowPos(ImVec2(hc.x - hit * 0.5f, hc.y - hit * 0.5f));
    ImGui::SetNextWindowSize(ImVec2(hit, hit));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    bool clicked = false, hovered = false;
    if (ImGui::Begin("##offscreenArrow", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoSavedSettings |
                         ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove |
                         ImGuiWindowFlags_NoBringToFrontOnFocus)) {
        clicked = ImGui::InvisibleButton("##go", ImVec2(hit, hit));
        hovered = ImGui::IsItemHovered();
        if (hovered) {
            const std::string tipTxt = sel->name() + "\nEl objeto seleccionado está fuera de vista. Clic: llevar la cámara hasta él.";
            richTooltip(tipTxt.c_str());
        }
    }
    ImGui::End();
    ImGui::PopStyleVar(2);

    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    dl->PushClipRect(ImVec2(vp.x, vp.y), ImVec2(vp.x + vp.w, vp.y + vp.h), true);
    const ImVec2 p1(base.x + perp.x * half, base.y + perp.y * half);
    const ImVec2 p2(base.x - perp.x * half, base.y - perp.y * half);
    // Como en C4D R19: punta de flecha azul clara, sin borde, con la cola
    // hundida y dos caras (una mas clara) para que parezca un cono plano.
    const ImVec2 notch(base.x + d.x * 5.0f, base.y + d.y * 5.0f);
    const ImU32 light = hovered ? IM_COL32(185, 220, 255, 255) : IM_COL32(140, 185, 235, 255);
    const ImU32 dark = hovered ? IM_COL32(120, 170, 235, 255) : IM_COL32(75, 125, 195, 255);
    dl->AddTriangleFilled(tip, p1, notch, light);
    dl->AddTriangleFilled(tip, notch, p2, dark);
    dl->PopClipRect();

    if (clicked) startCamTrip(camera, *sel);
}

// --- Gestor de Objetos (seccion 7) --------------------------------------------------

struct TreeRow {
    SceneNode* node;
    int depth;
};

void collectRows(SceneNode& node, int depth, std::vector<TreeRow>& out) {
    for (const auto& c : node.children()) {
        out.push_back({c.get(), depth});
        if (!c->children().empty() && !g.collapsed.count(c->id())) collectRows(*c, depth + 1, out);
    }
}

void addChildMenu(SceneNode& root, SceneNode* parent, SceneNode*& selected, SceneHistory& history) {
    for (const auto& it : kPrimitives) {
        if (!it.functional) continue;
        if (ImGui::MenuItem(it.name)) {
            history.commitBefore(root);
            selected = parent->addChild(defaultNameFor(it.type), it.type);
            g.collapsed.erase(parent->id());
            g.focus = AttrFocus::Object;
        }
    }
    if (ImGui::MenuItem("Luz")) {
        history.commitBefore(root);
        selected = parent->addChild("Luz", PrimitiveType::Light);
        g.collapsed.erase(parent->id());
    }
}

void drawObjectManager(SceneNode& root, SceneNode*& selected, SceneHistory& history,
                       std::vector<Material>& materials, ImVec2 pos, ImVec2 size) {
    beginPanel("##objmgr", pos, size, ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoScrollbar);
    if (ImGui::BeginMenuBar()) {
        panelGrip(names().objects);
        if (ImGui::BeginMenu("Archivo")) {
            disabledItems({{"Fusionar Objetos...", ""}});
            if (ImGui::MenuItem("Guardar Objetos Seleccionados como...", nullptr, false,
                                selected != nullptr && cinewareAvailable()))
                exportSelectedC4D(root, selected, materials);
            if (ImGui::MenuItem("Guardar como Preestablecido...", nullptr, false,
                                selected != nullptr && cinewareAvailable()))
                beginSavePreset(*selected, materials);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Editar")) {
            if (ImGui::MenuItem("Eliminar", "Supr", false, selected != nullptr)) deleteSelected(root, selected, history);
            if (ImGui::MenuItem("Expandir Todo")) g.collapsed.clear();
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Ver")) {
            disabledItems({{"Modo Árbol", ""}, {"Modo Lista", ""}, {nullptr, nullptr}, {"Mostrar Etiquetas", ""}});
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Objetos")) {
            if (ImGui::BeginMenu("Añadir")) {
                addChildMenu(root, &root, selected, history);
                ImGui::EndMenu();
            }
            if (ImGui::MenuItem("Duplicar", nullptr, false, selected != nullptr)) {
                history.commitBefore(root);
                selected = duplicateSubtree(*selected->parent(), *selected);
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Etiquetas")) {
            const bool canTag = selected && hasGeometry(selected->primitive) && !materials.empty();
            if (ImGui::BeginMenu("Etiqueta de Textura", canTag)) {
                for (size_t i = 0; i < materials.size(); ++i)
                    if (ImGui::MenuItem(materials[i].name.c_str())) {
                        history.commitBefore(root);
                        selected->materialIndex = (int)i;
                    }
                ImGui::EndMenu();
            }
            ImGui::EndMenu();
        }
        ImGui::BeginMenu("Favoritos", false);
        const float ib = ImGui::GetFrameHeight() - 4.0f;
        ImGui::SameLine(ImGui::GetWindowWidth() - 2 * (ib + 4) - 8);
        iconButton("omsearch", Icon::Search, ib, false, false, "Buscar (todavía no implementado)", nullptr);
        ImGui::SameLine(0, 4);
        iconButton("omfilter", Icon::Filter, ib, false, false, "Filtrar (todavía no implementado)", nullptr);
        ImGui::EndMenuBar();
    }

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    ImGui::BeginChild("##omtree", ImVec2(0, 0), ImGuiChildFlags_None);
    std::vector<TreeRow> rows;
    collectRows(root, 0, rows);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float rowH = 20.0f;
    const float fullW = ImGui::GetContentRegionAvail().x;
    const float tagsW = 46.0f;
    const float flagsW = 34.0f;
    SceneNode* pendingDelete = nullptr;
    SceneNode* pendingDuplicate = nullptr;
    SceneNode* pendingGroup = nullptr;
    SceneNode* pendingDissolve = nullptr;

    for (size_t r = 0; r < rows.size(); ++r) {
        SceneNode* node = rows[r].node;
        const int depth = rows[r].depth;
        ImGui::PushID(node->id());
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const ImVec2 q(p.x + fullW, p.y + rowH);

        ImGui::SetNextItemAllowOverlap();
        ImGui::InvisibleButton("##row", ImVec2(fullW, rowH), ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
        const bool rowHovered = ImGui::IsItemHovered();
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left) || ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
            if (ImGui::GetIO().KeyCtrl && ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
                // Ctrl+clic: anade o quita de la seleccion (como en C4D).
                if (g.selIds.count(node->id()) && g.selIds.size() > 1) {
                    g.selIds.erase(node->id());
                    if (node == selected) selected = firstSelected(root);
                } else {
                    g.selIds.insert(node->id());
                    selected = node;
                }
                g.lastSelectedId = selected ? selected->id() : -1;
            } else {
                selected = node;
            }
            g.focus = AttrFocus::Object;
        }
        if (rowHovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            g.renamingNodeId = node->id();
            std::snprintf(g.renameBuf, sizeof(g.renameBuf), "%s", node->name().c_str());
            g.renameFocus = true;
        }
        if (rowHovered) g.hint = "Clic: seleccionar. Ctrl+Clic: anadir o quitar de la seleccion. Doble clic: renombrar. Clic derecho: menú contextual. Arrastre un material aquí para asignarlo.";
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("IRON3D_MAT")) {
                const int mi = *(const int*)pl->Data;
                if (hasGeometry(node->primitive) && mi >= 0 && (size_t)mi < materials.size()) {
                    history.commitBefore(root);
                    node->materialIndex = mi;
                }
            }
            ImGui::EndDragDropTarget();
        }
        // Menu contextual con la estructura del de C4D R19 (mismos grupos y
        // orden); lo que Iron3D todavia no tiene sale deshabilitado.
        if (ImGui::BeginPopupContextItem("##ctx")) {
            // Etiquetas: solo hay Etiqueta de Textura (el material asignado).
            if (ImGui::BeginMenu("CINEMA 4D Etiquetas")) {
                const bool canTag = hasGeometry(node->primitive) && !materials.empty();
                if (ImGui::BeginMenu("Etiqueta de Textura", canTag)) {
                    for (size_t i = 0; i < materials.size(); ++i)
                        if (ImGui::MenuItem(materials[i].name.c_str(), nullptr, node->materialIndex == (int)i)) {
                            history.commitBefore(root);
                            node->materialIndex = (int)i;
                        }
                    ImGui::EndMenu();
                }
                if (ImGui::MenuItem("Eliminar Etiqueta de Textura", nullptr, false, node->materialIndex >= 0)) {
                    history.commitBefore(root);
                    node->materialIndex = -1;
                }
                ImGui::Separator();
                disabledItems({{"Fong", ""}, {"Compositing", ""}, {"Protección", ""}, {"Mostrar", ""},
                               {"Objetivo", ""}, {"Restricción", ""}});
                ImGui::EndMenu();
            }
            for (const char* fam : {"Character Etiquetas", "Cineman Etiquetas", "Etiqueta UVW", "Hair Etiquetas",
                                    "MoGraph Etiquetas", "Motion Tracker Etiquetas", "Scripting Etiquetas",
                                    "Simulación Etiquetas", "Sketch Etiquetas", "Videocámara Etiquetas"})
                if (ImGui::BeginMenu(fam, false)) ImGui::EndMenu();
            ImGui::Separator();
            if (ImGui::BeginMenu("Cargar Etiqueta Preestablecida", false)) ImGui::EndMenu();
            if (ImGui::BeginMenu("Restaurar Selección", false)) ImGui::EndMenu();
            ImGui::Separator();
            iconMenuItem(Icon::MnuLayers, "Añadir a una Capa Nueva", nullptr, false, false);
            iconMenuItem(Icon::MnuLayers, "Gestor de Capas...", "Mayús+F4", false, false);
            ImGui::Separator();
            disabledItems({{"Eliminar de Todas las Tomas", ""}, {"Gestor de Tomas...", ""}});
            ImGui::Separator();
            if (ImGui::MenuItem("Seleccionar Hijos", nullptr, false, !node->children().empty())) {
                forSubtree(*node, [](SceneNode& n) { g.selIds.insert(n.id()); });
                selected = node;
                g.lastSelectedId = node->id();
                g.focus = AttrFocus::Object;
            }
            ImGui::MenuItem("Establecer como Raíz", nullptr, false, false);
            ImGui::Separator();
            if (ImGui::MenuItem("Expandir Todo", nullptr, false, !node->children().empty()))
                forSubtree(*node, [](SceneNode& n) { g.collapsed.erase(n.id()); });
            if (ImGui::MenuItem("Contraer Todo", nullptr, false, !node->children().empty()))
                forSubtree(*node, [](SceneNode& n) {
                    if (!n.children().empty()) g.collapsed.insert(n.id());
                });
            ImGui::Separator();
            if (iconMenuItem(Icon::MakeEditable, "Hacer Editable", "C", false, canMakeEditable(root, selected, node))) {
                const int n = makeEditable(root, selected, history, node);
                if (n > 0) flashStatus("Hacer Editable: " + std::to_string(n) + (n == 1 ? " objeto convertido." : " objetos convertidos."));
            }
            if (ImGui::MenuItem("Estado Actual del Objeto")) { if (!g.selIds.count(node->id())) selectOnly(selected, node); g.pendingCmd = UiCmd::CurrentState; }
            if (ImGui::MenuItem("Conectar Objetos")) { if (!g.selIds.count(node->id())) selectOnly(selected, node); g.pendingCmd = UiCmd::Connect; }
            if (ImGui::MenuItem("Conectar+Eliminar")) { if (!g.selIds.count(node->id())) selectOnly(selected, node); g.pendingCmd = UiCmd::ConnectDelete; }
            ImGui::Separator();
            if (iconMenuItem(Icon::Null, "Agrupar Objetos", "Alt+G")) pendingGroup = node;
            if (ImGui::MenuItem("Expandir Grupo de Objetos", "Mayús+G", false, !node->children().empty()))
                pendingDissolve = node;
            if (ImGui::MenuItem("Eliminar Sin Hijos", nullptr, false, true)) pendingDissolve = node;
            ImGui::MenuItem("Convertir Selección de Objetos a XRef", nullptr, false, false);
            ImGui::Separator();
            disabledItems({{"Mostrar Pistas...", ""}, {"Mostrar F-Curves...", ""}, {"Mostrar Movimientos...", ""}});
            ImGui::Separator();
            ImGui::MenuItem("Información del Objeto...", nullptr, false, false);
            if (iconMenuItem(Icon::MnuProject, "Información del Proyecto", "Ctrl+I")) g.focus = AttrFocus::Project;
            ImGui::Separator();
            iconMenuItem(Icon::MnuHelp, "Mostrar Ayuda...", "Ctrl+F1", false, false);
            // Lo propio de Iron3D (en C4D vive en otros menus), al final.
            ImGui::Separator();
            if (ImGui::BeginMenu("Añadir Hijo")) {
                addChildMenu(root, node, selected, history);
                ImGui::EndMenu();
            }
            if (ImGui::MenuItem("Renombrar")) {
                g.renamingNodeId = node->id();
                std::snprintf(g.renameBuf, sizeof(g.renameBuf), "%s", node->name().c_str());
                g.renameFocus = true;
            }
            if (ImGui::MenuItem("Duplicar")) pendingDuplicate = node;
            if (iconMenuItem(Icon::MnuDelete, "Eliminar", "Supr")) pendingDelete = node;
            if (ImGui::MenuItem("Guardar como Preestablecido...", nullptr, false, cinewareAvailable()))
                beginSavePreset(*node, materials);
            ImGui::EndPopup();
        }

        // Fondo de fila
        // En C4D la fila seleccionada no se rellena: solo el nombre pasa a naranja.
        const bool nodeSel = g.selIds.count(node->id()) > 0;
        if (rowHovered && !nodeSel) dl->AddRectFilled(p, q, kHoverRow);

        // Columnas (separadores verticales como en C4D)
        const float tagsX = q.x - tagsW;
        const float flagsX = tagsX - flagsW;
        dl->AddLine(ImVec2(flagsX - 1, p.y), ImVec2(flagsX - 1, q.y), kBorderDark);
        dl->AddLine(ImVec2(tagsX - 1, p.y), ImVec2(tagsX - 1, q.y), kBorderDark);

        // Expandir/plegar
        const float x0 = p.x + 4 + depth * 16.0f;
        if (!node->children().empty()) {
            const bool open = !g.collapsed.count(node->id());
            ImGui::SetCursorScreenPos(ImVec2(x0, p.y));
            ImGui::SetNextItemAllowOverlap();
            if (ImGui::InvisibleButton("##exp", ImVec2(12, rowH))) {
                if (open) g.collapsed.insert(node->id());
                else g.collapsed.erase(node->id());
            }
            const float cy = p.y + rowH * 0.5f;
            if (open) dl->AddTriangleFilled(ImVec2(x0 + 1, cy - 3), ImVec2(x0 + 9, cy - 3), ImVec2(x0 + 5, cy + 3), kTextCol);
            else dl->AddTriangleFilled(ImVec2(x0 + 2, cy - 4), ImVec2(x0 + 8, cy), ImVec2(x0 + 2, cy + 4), kTextCol);
        }
        icons::draw(dl, iconFor(node->primitive), ImVec2(x0 + 14, p.y + 2), 16, true);

        // Nombre (o campo de renombrar)
        const float nameX = x0 + 36;
        if (g.renamingNodeId == node->id()) {
            ImGui::SetCursorScreenPos(ImVec2(nameX, p.y + 1));
            ImGui::SetNextItemWidth(std::max(60.0f, flagsX - nameX - 6));
            if (g.renameFocus) {
                ImGui::SetKeyboardFocusHere();
                g.renameFocus = false;
            }
            const bool done = ImGui::InputText("##rename", g.renameBuf, sizeof(g.renameBuf),
                                               ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
            if (done || ImGui::IsItemDeactivated()) {
                if (std::strlen(g.renameBuf) > 0 && node->name() != g.renameBuf) {
                    history.commitBefore(root);
                    node->setName(g.renameBuf);
                }
                g.renamingNodeId = -1;
            }
        } else {
            dl->PushClipRect(ImVec2(nameX, p.y), ImVec2(flagsX - 4, q.y), true);
            dl->AddText(ImVec2(nameX, p.y + (rowH - ImGui::GetTextLineHeight()) * 0.5f),
                        nodeSel ? kSelOrange : kTextCol, node->name().c_str());
            dl->PopClipRect();
        }

        // Dos puntos de visibilidad independientes (seccion 7): el de ARRIBA
        // es el visor (editor) y el de ABAJO el render. Cada clic avanza
        // Por Defecto (gris) -> Activado (verde) -> Apagado (rojo), como C4D.
        const ImVec2 dotC(flagsX + 9, p.y + rowH * 0.5f);
        auto visDot = [&](const char* id, VisMode& mode, float yOff, const char* what) {
            ImGui::SetCursorScreenPos(ImVec2(flagsX + 2, p.y + (yOff < 0 ? 0.0f : rowH * 0.5f)));
            ImGui::SetNextItemAllowOverlap();
            if (ImGui::InvisibleButton(id, ImVec2(14, rowH * 0.5f))) {
                history.commitBefore(root);
                mode = (VisMode)(((int)mode + 1) % 3);
            }
            static const char* kNames[] = {"Por Defecto", "Activado", "Apagado"};
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Visible en %s: %s\n(clic para cambiar)", what, kNames[(int)mode]);
                g.hint = "Punto superior: visible en el editor. Punto inferior: visible en el render. Gris = Por Defecto (hereda del padre), verde = Activado, rojo = Apagado.";
            }
            const ImU32 col = mode == VisMode::On    ? IM_COL32(70, 200, 80, 255)
                              : mode == VisMode::Off ? IM_COL32(220, 60, 50, 255)
                                                     : IM_COL32(150, 150, 150, 255);
            dl->AddCircleFilled(ImVec2(dotC.x, dotC.y + yOff), 3.0f, col);
        };
        visDot("##visEd", node->editorVis, -4.0f, "Editor");
        visDot("##visRd", node->renderVis, 4.0f, "Render");
        icons::draw(dl, Icon::Check, ImVec2(flagsX + 17, p.y + 3), 14, true);

        // Etiquetas: Phong (todas las primitivas con geometria) + Textura.
        float tx = tagsX + 3;
        if (hasGeometry(node->primitive)) {
            icons::draw(dl, Icon::PhongTag, ImVec2(tx, p.y + 2), 16, true);
            tx += 18;
        }
        if (node->materialIndex >= 0 && (size_t)node->materialIndex < materials.size()) {
            const Material& m = materials[(size_t)node->materialIndex];
            ImGui::SetCursorScreenPos(ImVec2(tx, p.y));
            ImGui::SetNextItemAllowOverlap();
            if (ImGui::InvisibleButton("##tag", ImVec2(18, rowH))) {
                selected = node;
                g.focus = AttrFocus::Tag;
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Etiqueta de Textura [%s]", m.name.c_str());
            const bool tagSel = g.focus == AttrFocus::Tag && node == selected;
            if (tagSel) dl->AddRect(ImVec2(tx - 1, p.y + 1), ImVec2(tx + 17, p.y + rowH - 1), kAmber, 2.0f);
            float previewSpec, previewShin;
            materialBallSpecShininess(m, previewSpec, previewShin);
            icons::materialBall(dl, ImVec2(tx + 8, p.y + rowH * 0.5f), 7.0f,
                                ImVec4(m.baseColor.x, m.baseColor.y, m.baseColor.z, 1.0f), previewSpec, previewShin);
        }

        ImGui::SetCursorScreenPos(ImVec2(p.x, q.y));
        ImGui::PopID();
    }

    // Area vacia: clic deselecciona, clic derecho anade objetos en la raiz.
    const ImVec2 rest = ImGui::GetContentRegionAvail();
    if (rest.y > 4) {
        ImGui::InvisibleButton("##empty", ImVec2(fullW, rest.y), ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) selected = nullptr;
        if (ImGui::BeginPopupContextItem("##emptyctx")) {
            if (ImGui::BeginMenu("Añadir")) {
                addChildMenu(root, &root, selected, history);
                ImGui::EndMenu();
            }
            ImGui::EndPopup();
        }
    }
    g.objMgrHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::End();

    if (pendingGroup) groupObjects(root, selected, history, pendingGroup);
    if (pendingDissolve) dissolveNode(root, selected, history, pendingDissolve);
    if (pendingDuplicate && pendingDuplicate->parent()) {
        history.commitBefore(root);
        selected = duplicateSubtree(*pendingDuplicate->parent(), *pendingDuplicate);
    }
    if (pendingDelete && pendingDelete->parent()) {
        history.commitBefore(root);
        pendingDelete->parent()->removeChild(pendingDelete);
        selected = nullptr;
    }
}

// Pestanas verticales del borde derecho (seccion 8). Devuelve la pestana
// pulsada (-1 ninguna); solo responden las de `enabledMask`.
int drawVerticalTabs(ImVec2 pos, ImVec2 size, const char* const* names, int count, int active,
                     unsigned enabledMask = 1u) {
    int clickedTab = -1;
    beginPanel(names[0], pos, size, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float y = pos.y + 4;
    for (int i = 0; i < count; ++i) {
        const float len = icons::textVerticalLength(names[i]) + 14.0f;
        const ImVec2 a(pos.x + 1, y), b(pos.x + size.x - 1, y + len);
        ImGui::SetCursorScreenPos(a);
        ImGui::PushID(i);
        if (ImGui::InvisibleButton("##vt", ImVec2(b.x - a.x, len)) && (enabledMask & (1u << i))) clickedTab = i;
        const bool hov = ImGui::IsItemHovered();
        if (hov && i != active && !(enabledMask & (1u << i)))
            ImGui::SetTooltip("%s (todavía no implementado)", names[i]);
        ImGui::PopID();
        dl->AddRectFilled(a, b, i == active ? kTabOn : kTabOff);
        dl->AddRect(a, b, kBorderDark);
        icons::textVertical(dl, ImVec2(b.x - 3, a.y + 7), i == active ? kTextCol : kTextDim, names[i]);
        y += len + 2;
    }
    ImGui::End();
    return clickedTab;
}

// --- Gestor de Atributos (secciones 6, 15, 17.3, 18.2, 19.3) --------------------

void attrHeaderNav() {
    const float ib = ImGui::GetFrameHeight() - 4.0f;
    ImGui::SameLine(ImGui::GetWindowWidth() - 5 * (ib + 3) - 8);
    iconButton("anback", Icon::Back, ib, false, false, "Atrás", nullptr);
    ImGui::SameLine(0, 3);
    iconButton("anfwd", Icon::Forward, ib, false, false, "Adelante", nullptr);
    ImGui::SameLine(0, 3);
    iconButton("anup", Icon::Up, ib, false, false, "Subir", nullptr);
    ImGui::SameLine(0, 3);
    iconButton("anlock", Icon::Lock, ib, false, false, "Bloquear Elemento", nullptr);
    ImGui::SameLine(0, 3);
    iconButton("annew", Icon::NewPanel, ib, false, false, "Nuevo Gestor de Atributos", nullptr);
}

void titleLine(Icon icon, const char* title) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    icons::draw(dl, icon, ImVec2(p.x + 2, p.y + 1), 18, true);
    if (g_fontBold) ImGui::PushFont(g_fontBold);
    dl->AddText(ImVec2(p.x + 26, p.y + 2), kTextCol, title);
    if (g_fontBold) ImGui::PopFont();
    ImGui::Dummy(ImVec2(ImGui::GetContentRegionAvail().x, 22));
}

void drawProjectSettings() {
    titleLine(Icon::Gear, "Proyecto");
    static unsigned mask = 1u;
    const char* tabs[] = {"Configuración del Proyecto", "Información", "Dinámicas", "Referenciar", "A Realizar",
                          "Interpolación de Claves"};
    attrTabRow(tabs, IM_ARRAYSIZE(tabs), mask);
    if (tabOn(mask, 0)) {
        sectionHeader("Configuración del Proyecto");
        const float savedLabelW = g_labelW;
        const float fullW = ImGui::GetContentRegionAvail().x;
        // Dos columnas como en C4D (pares FPS | Tiempo del Proyecto, Min | Max...);
        // si el panel es estrecho, una sola.
        const bool twoCols = fullW >= 380.0f;
        const float colW = twoCols ? std::floor(fullW * 0.5f) : fullW;
        const float startX = ImGui::GetCursorPosX();
        auto col = [&](int c) {  // coloca el cursor en la columna c de la fila actual
            g_labelW = std::floor(colW * (twoCols ? 0.58f : 0.55f));
            if (c == 1 && twoCols) ImGui::SameLine(startX + colW);
        };
        const float numW = std::min(80.0f, colW - g_labelW - 10.0f);

        g_labelW = std::floor(colW * 0.58f);
        propLabel("Escala del Proyecto", false);
        ImGui::BeginDisabled();
        ImGui::SetNextItemWidth(50);
        float one = 1.0f;
        ImGui::DragFloat("##scale", &one, 1, 0, 0, "%.0f");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(std::min(110.0f, ImGui::GetContentRegionAvail().x - 4.0f));
        if (ImGui::BeginCombo("##unit", "Centímetros")) ImGui::EndCombo();
        ImGui::Button("Escalar Proyecto...", ImVec2(std::min(fullW - 8.0f, 260.0f), 0));
        ImGui::EndDisabled();
        setHint("Escalar Proyecto: todavía no implementado en Iron3D.");
        ImGui::Separator();

        col(0);
        propLabel("FPS", false);
        ImGui::SetNextItemWidth(numW);
        if (ImGui::DragInt("##fps", &g.fps, 0.2f, 1, 240)) g.fps = std::clamp(g.fps, 1, 240);
        setHint("Fotogramas por segundo: velocidad de reproducción de la Línea de Tiempo.");
        col(1);
        propLabel("Tiempo del Proyecto", false);
        int cur = (int)std::lround(g.frame);
        ImGui::SetNextItemWidth(numW);
        if (ImGui::DragInt("##cur", &cur, 0.2f, g.minFrame, g.maxFrame, "%d F")) g.frame = (float)cur;
        col(0);
        propLabel("Tiempo Mínimo", false);
        ImGui::SetNextItemWidth(numW);
        if (ImGui::DragInt("##min", &g.minFrame, 0.2f, -100000, g.maxFrame - 1, "%d F")) g.previewMin = std::max(g.previewMin, g.minFrame);
        col(1);
        propLabel("Tiempo Máximo", false);
        ImGui::SetNextItemWidth(numW);
        if (ImGui::DragInt("##max", &g.maxFrame, 0.2f, g.minFrame + 1, 100000, "%d F")) g.previewMax = std::min(g.previewMax, g.maxFrame);
        col(0);
        propLabel("Tiempo Mínimo de Previo", false);
        ImGui::SetNextItemWidth(numW);
        ImGui::DragInt("##pmin", &g.previewMin, 0.2f, g.minFrame, g.previewMax, "%d F");
        col(1);
        propLabel("Tiempo Máximo de Previo", false);
        ImGui::SetNextItemWidth(numW);
        ImGui::DragInt("##pmax", &g.previewMax, 0.2f, g.previewMin, g.maxFrame, "%d F");
        ImGui::Separator();

        auto roFloatN = [&](const char* label, float v, const char* fmt) {
            propLabel(label, false);
            ImGui::BeginDisabled();
            ImGui::SetNextItemWidth(numW);
            ImGui::PushID(label);
            ImGui::DragFloat("##v", &v, 1.0f, 0.0f, 0.0f, fmt);
            ImGui::PopID();
            ImGui::EndDisabled();
            setHint("Parámetro todavía no implementado en Iron3D");
        };
        col(0); roFloatN("Nivel de Detalle", 100.0f, "%.0f %%");
        col(1); roCheck("Nivel de Detalle del Render en el Editor", false, false);
        ImGui::Separator();
        col(0); roCheck("Usar Animación", true, false);
        col(1); roCheck("Usar Expresiones", true, false);
        col(0); roCheck("Usar Generadores", true, false);
        col(1); roCheck("Usar Deformadores", true, false);
        col(0); roCheck("Usar Sistema de Movimiento", true, false);
        ImGui::Separator();

        g_labelW = std::floor(fullW * 0.45f);
        roCombo("Color Objetos por Defecto", "Gris-Azul");
        roCombo("Recortar Vista", "Medio");
        roCheck("Flujo de Trabajo Lineal", true, false);
        roCombo("Introducir Perfil de Color", "sRGB");
        g_labelW = savedLabelW;

        ImGui::Spacing();
        ImGui::BeginDisabled();
        const float bw = std::floor((fullW - 12.0f) * 0.5f);
        ImGui::Button("Cargar Preestablecido...", ImVec2(bw, 0));
        ImGui::SameLine();
        ImGui::Button("Guardar Preestablecido...", ImVec2(bw, 0));
        ImGui::EndDisabled();
        setHint("Preestablecidos de proyecto: todavía no implementados en Iron3D.");
    }
    if (mask & ~1u) {
        sectionHeader("Página no disponible");
        ImGui::TextDisabled("  Todavía no implementado en Iron3D.");
    }
}

void drawObjectAttributes(SceneNode& node, SceneNode& root, SceneHistory& history) {
    const bool isLight = node.primitive == PrimitiveType::Light;
    const bool isNull = node.primitive == PrimitiveType::None;
    const int kind = isLight ? 1 : (isNull ? 2 : 0);
    unsigned& mask = g.objMask[kind];

    char title[200];
    std::snprintf(title, sizeof(title), "Objeto %s [%s]", defaultNameFor(node.primitive), node.name().c_str());
    titleLine(iconFor(node.primitive), title);

    static const char* geoTabs[] = {"Básico", "Coord.", "Objeto", "Phong"};
    static const char* lightTabs[] = {"Básico", "Coord.", "General", "Detalles", "Visibilidad", "Sombra",
                                      "Fotométrico", "Cáusticas", "Ruido", "Lentes", "Proyecto"};
    static const char* nullTabs[] = {"Básico", "Coord.", "Objeto"};
    if (isLight) attrTabRow(lightTabs, IM_ARRAYSIZE(lightTabs), mask);
    else if (isNull) attrTabRow(nullTabs, IM_ARRAYSIZE(nullTabs), mask);
    else attrTabRow(geoTabs, IM_ARRAYSIZE(geoTabs), mask);

    if (tabOn(mask, 0)) {
        sectionHeader("Propiedades Básicas");
        propLabel("Nombre", false);
        char nameBuf[128];
        std::snprintf(nameBuf, sizeof(nameBuf), "%s", node.name().c_str());
        ImGui::SetNextItemWidth(fieldWidth(220));
        if (ImGui::InputText("##name", nameBuf, sizeof(nameBuf)) && nameBuf[0]) node.setName(nameBuf);
        trackEdit(history, root);
        roCombo("Capa", "");
        const char* visItems[] = {"Por Defecto", "Activado", "Apagado"};
        auto visCombo = [&](const char* label, const char* id, VisMode& mode, const char* hint) {
            propLabel(label);
            int v = (int)mode;
            ImGui::SetNextItemWidth(fieldWidth(140));
            if (ImGui::Combo(id, &v, visItems, 3)) {
                history.commitBefore(root); // `mode` aun no ha cambiado aqui
                mode = (VisMode)v;
            }
            setHint(hint);
        };
        visCombo("Visible en Editor", "##vised", node.editorVis,
                 "Visible en Editor (punto superior): Por Defecto hereda del padre, Activado lo muestra siempre, Apagado lo oculta en el visor.");
        visCombo("Visible en Render", "##visrd", node.renderVis,
                 "Visible en Render (punto inferior): Iron3D todavía no tiene render final; el valor se guarda en la escena.");
        roCombo("Usar Color", "Desactivado");
        roCheck("Activado", true);
        roCheck("Rayos X", false);
    }
    if (tabOn(mask, 1)) {
        sectionHeader("Coordenadas");
        if (ImGui::BeginTable("##coord", 3, ImGuiTableFlags_SizingStretchSame)) {
            const char* pl[] = {"P . X", "P . Y", "P . Z"};
            const char* sl[] = {"S . X", "S . Y", "S . Z"};
            const char* rl[] = {"R . H", "R . P", "R . B"};
            // HPB de C4D: H gira en Y, P en X, B en Z.
            const int rotIdx[] = {1, 0, 2};
            for (int i = 0; i < 3; ++i) {
                ImGui::TableNextRow();
                ImGui::PushID(i);
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(pl[i]);
                ImGui::SameLine(44);
                float cm = node.position[i] * kCmPerUnit;
                ImGui::SetNextItemWidth(-1);
                if (ImGui::DragFloat("##p", &cm, 1.0f, 0, 0, "%.0f cm")) node.position[i] = cm / kCmPerUnit;
                trackEdit(history, root);
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(sl[i]);
                ImGui::SameLine(44);
                ImGui::SetNextItemWidth(-1);
                ImGui::DragFloat("##s", &node.scale[i], 0.01f, 0.001f, 1000.0f, "%.3f");
                trackEdit(history, root);
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(rl[i]);
                ImGui::SameLine(44);
                ImGui::SetNextItemWidth(-1);
                ImGui::DragFloat("##r", &node.rotationDeg[rotIdx[i]], 0.5f, 0, 0, "%.1f °");
                trackEdit(history, root);
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        roCombo("Orden", "HPB");
        sectionHeader("Congelar Transformación");
        ImGui::TextDisabled("  Todavía no implementado en Iron3D.");
    }
    if (tabOn(mask, 2)) {
        if (isLight) {
            sectionHeader("General");
            propLabel("Color");
            ImGui::BeginDisabled();
            float white[3] = {1, 1, 1};
            ImGui::ColorEdit3("##lc", white, ImGuiColorEditFlags_NoInputs);
            ImGui::EndDisabled();
            roFloat("Intensidad", 100.0f, "%.0f %%");
            roCombo("Tipo", "Omni");
            roCombo("Sombra", "Ninguno");
            roCombo("Luz Visible", "Ninguno");
            roCheck("No Ilumina", false);
            roCheck("Mostrar Iluminación", true, false);
            roCheck("Difusión", true);
            roCheck("Mostrar Luz Visible", false, false);
            roCheck("Especular", true);
            roCheck("Iluminación de IG", true);
            ImGui::TextDisabled("  Iron3D: la luz ilumina con color e intensidad fijos por ahora.");
        } else if (isNull) {
            sectionHeader("Propiedades del Objeto");
            roCombo("Mostrar", "Punto");
            roFloat("Radio", 10.0f, "%.0f cm");
            roFloat("Relación de Aspecto", 1.0f, "%.2f");
            roCombo("Orientación", "Cámara");
        } else {
            sectionHeader("Propiedades del Objeto");
            if (node.primitive == PrimitiveType::Cube) {
                roFloat("Tamaño . X", 200, "%.0f cm");
                roFloat("Tamaño . Y", 200, "%.0f cm");
                roFloat("Tamaño . Z", 200, "%.0f cm");
                roFloat("Segmentos en X", 1, "%.0f");
                roFloat("Segmentos en Y", 1, "%.0f");
                roFloat("Segmentos en Z", 1, "%.0f");
                roCheck("Superficies Separadas", false);
                roCheck("Canto", false);
            } else if (node.primitive == PrimitiveType::Mesh) {
                const MeshData* m = node.mesh.get();
                roFloat("Vértices", m ? (float)m->vertices.size() : 0.0f, "%.0f");
                roFloat("Triángulos", m ? (float)(m->indices.size() / 3) : 0.0f, "%.0f");
                ImGui::TextDisabled("  Objeto Poligonal (importado o hecho editable con C).");
                ImGui::TextDisabled("  Puntos, aristas y polígonos: botones de la barra izquierda (Supr borra).");
            } else if (node.primitive == PrimitiveType::Sphere) {
                roFloat("Radio", 100, "%.0f cm");
                roFloat("Segmentos", 24, "%.0f");
                roCombo("Tipo", "Estándar");
                roCheck("Perfectos de Render", true);
            } else {
                roFloat("Anchura", 200, "%.0f cm");
                roFloat("Altura", 200, "%.0f cm");
                roFloat("Segmentos en la Anchura", 1, "%.0f");
                roFloat("Segmentos en la Altura", 1, "%.0f");
                roCombo("Orientación", "+Y");
            }
            ImGui::TextDisabled("  Para cambiar el tamaño use Escala (Coord.) o el Gestor de Coordenadas.");
        }
    }
    if (!isNull && !isLight && tabOn(mask, 3)) {
        sectionHeader("Etiqueta Phong");
        roCheck("Limitar Ángulo", true);
        roFloat("Ángulo de Phong", 80.0f, "%.0f °");
        roCheck("Usar Bordes", false);
    }
    if (isLight && (mask & ~7u)) {
        sectionHeader("Página no disponible");
        ImGui::TextDisabled("  Todavía no implementado en Iron3D.");
    }
}

void drawMaterialAttributes(Material& m, int index, SceneNode& root, SceneHistory& history) {
    char title[200];
    std::snprintf(title, sizeof(title), "Material [%s]", m.name.c_str());
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 p = ImGui::GetCursorScreenPos();
        float previewSpec, previewShin;
        materialBallSpecShininess(m, previewSpec, previewShin);
        icons::materialBall(dl, ImVec2(p.x + 11, p.y + 11), 9, ImVec4(m.baseColor.x, m.baseColor.y, m.baseColor.z, 1), previewSpec, previewShin);
        if (g_fontBold) ImGui::PushFont(g_fontBold);
        dl->AddText(ImVec2(p.x + 26, p.y + 2), kTextCol, title);
        if (g_fontBold) ImGui::PopFont();
        ImGui::Dummy(ImVec2(ImGui::GetContentRegionAvail().x, 22));
    }
    static const char* tabs[] = {"Básico", "Color", "Reflectancia", "Iluminación", "Editor", "Asignar"};
    attrTabRow(tabs, IM_ARRAYSIZE(tabs), g.matMask);

    if (tabOn(g.matMask, 0)) {
        sectionHeader("Propiedades Básicas");
        propLabel("Nombre", false);
        char nameBuf[128];
        std::snprintf(nameBuf, sizeof(nameBuf), "%s", m.name.c_str());
        ImGui::SetNextItemWidth(fieldWidth(220));
        if (ImGui::InputText("##mname", nameBuf, sizeof(nameBuf)) && nameBuf[0]) m.name = nameBuf;
        trackEdit(history, root);
        roCombo("Capa", "");
        roCheck("Color", true, false);
        roCheck("Difusión", false, false);
        roCheck("Luminancia", false, false);
        roCheck("Transparencia", false, false);
        roCheck("Reflectancia", true, false);
        roCheck("Entorno", false, false);
        roCheck("Relieve", false, false);
        roCheck("Alfa", false, false);
    }
    if (tabOn(g.matMask, 1)) {
        sectionHeader("Color");
        propLabel("Color");
        ImGui::SetNextItemWidth(fieldWidth(240));
        ImGui::ColorEdit3("##diff", &m.baseColor.x, ImGuiColorEditFlags_DisplayHSV | ImGuiColorEditFlags_PickerHueBar);
        trackEdit(history, root);
        roFloat("Brillo", 100.0f, "%.0f %%");
        roCombo("Textura", "");
        roCombo("Modelo", "Lambertiano");
    }
    if (tabOn(g.matMask, 2)) {
        sectionHeader("Reflectancia — PBR (Metallic/Roughness)");
        roCombo("Tipo", "GGX (Cook-Torrance)");
        propLabel("Metálico");
        float metallicPct = m.metallic * 100.0f;
        ImGui::SetNextItemWidth(fieldWidth());
        if (ImGui::SliderFloat("##metallic", &metallicPct, 0.0f, 100.0f, "%.0f %%")) m.metallic = metallicPct / 100.0f;
        trackEdit(history, root);
        propLabel("Rugosidad");
        float roughnessPct = m.roughness * 100.0f;
        ImGui::SetNextItemWidth(fieldWidth());
        if (ImGui::SliderFloat("##roughness", &roughnessPct, 0.0f, 100.0f, "%.0f %%")) m.roughness = roughnessPct / 100.0f;
        trackEdit(history, root);
        roCombo("Atenuación", "Añadir");
    }
    if (tabOn(g.matMask, 3) || tabOn(g.matMask, 4)) {
        sectionHeader(tabOn(g.matMask, 3) ? "Iluminación" : "Editor");
        ImGui::TextDisabled("  Todavía no implementado en Iron3D.");
    }
    if (tabOn(g.matMask, 5)) {
        sectionHeader("Asignar");
        std::vector<SceneNode*> users;
        collectUsers(root, index, users);
        if (users.empty()) ImGui::TextDisabled("  Ningún objeto usa este material. Arrástrelo al Gestor de Objetos.");
        for (SceneNode* u : users) {
            ImGui::PushID(u->id());
            icons::draw(ImGui::GetWindowDrawList(), iconFor(u->primitive), ImGui::GetCursorScreenPos(), 16, true);
            ImGui::Dummy(ImVec2(20, 16));
            ImGui::SameLine();
            ImGui::TextUnformatted(u->name().c_str());
            ImGui::PopID();
        }
    }
}

void drawTagAttributes(SceneNode& node, std::vector<Material>& materials, SceneNode& root, SceneHistory& history) {
    titleLine(Icon::Sphere, "Etiqueta de Textura [Textura]");
    static const char* tabs[] = {"Básico", "Etiqueta", "Coordenadas"};
    attrTabRow(tabs, IM_ARRAYSIZE(tabs), g.tagMask);
    if (tabOn(g.tagMask, 0)) {
        sectionHeader("Propiedades Básicas");
        roCombo("Nombre", "Textura");
    }
    if (tabOn(g.tagMask, 1)) {
        sectionHeader("Propiedades de Etiqueta");
        propLabel("Material", false);
        const char* cur = node.materialIndex >= 0 && (size_t)node.materialIndex < materials.size()
                              ? materials[(size_t)node.materialIndex].name.c_str()
                              : "";
        ImGui::SetNextItemWidth(fieldWidth(200));
        if (ImGui::BeginCombo("##tmat", cur)) {
            for (size_t i = 0; i < materials.size(); ++i)
                if (ImGui::Selectable(materials[i].name.c_str(), node.materialIndex == (int)i)) {
                    history.commitBefore(root);
                    node.materialIndex = (int)i;
                }
            ImGui::EndCombo();
        }
        roCombo("Selección", "");
        roCombo("Proyección", "Mapeado UVW");
        roCombo("Lado", "Ambos");
        roCheck("Mezclar Texturas", false);
        roCheck("Repetición", true);
        roCheck("Sin Costuras", false);
        roCheck("Usar UVW para Relieve", true);
        roFloat("Desfase U", 0, "%.0f %%");
        roFloat("Desfase V", 0, "%.0f %%");
        roFloat("Longitud U", 100, "%.0f %%");
        roFloat("Longitud V", 100, "%.0f %%");
        roFloat("Repeticiones U", 1, "%.0f");
        roFloat("Repeticiones V", 1, "%.0f");
    }
    if (tabOn(g.tagMask, 2)) {
        sectionHeader("Coordenadas");
        ImGui::TextDisabled("  Todavía no implementado en Iron3D.");
    }
}

void drawAttributesManager(SceneNode& root, SceneNode* selected, SceneHistory& history,
                           std::vector<Material>& materials, ImVec2 pos, ImVec2 size) {
    beginPanel("##attrmgr", pos, size, ImGuiWindowFlags_MenuBar);
    if (ImGui::BeginMenuBar()) {
        panelGrip(names().attributes);
        if (ImGui::BeginMenu("Modo")) {
            if (ImGui::MenuItem("Objeto", nullptr, g.focus == AttrFocus::Object)) g.focus = AttrFocus::Object;
            if (ImGui::MenuItem("Etiqueta", nullptr, g.focus == AttrFocus::Tag, selected && selected->materialIndex >= 0))
                g.focus = AttrFocus::Tag;
            if (ImGui::MenuItem("Material", nullptr, g.focus == AttrFocus::Material, g.selectedMat >= 0))
                g.focus = AttrFocus::Material;
            ImGui::Separator();
            if (ImGui::MenuItem("Proyecto", "Ctrl+D", g.focus == AttrFocus::Project)) g.focus = AttrFocus::Project;
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Editar")) {
            disabledItems({{"Copiar", ""}, {"Pegar", ""}, {nullptr, nullptr}, {"Seleccionar Todo", ""},
                           {"Mostrar Subcanales", ""}});
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Datos de Usuario")) {
            disabledItems({{"Añadir Datos de Usuario...", ""}, {"Gestionar Datos de Usuario...", ""}});
            ImGui::EndMenu();
        }
        attrHeaderNav();
        ImGui::EndMenuBar();
    }

    g_labelW = std::clamp(ImGui::GetContentRegionAvail().x * 0.42f, 120.0f, 220.0f);
    switch (g.focus) {
        case AttrFocus::Project:
            drawProjectSettings();
            break;
        case AttrFocus::Material:
            if (g.selectedMat >= 0 && (size_t)g.selectedMat < materials.size())
                drawMaterialAttributes(materials[(size_t)g.selectedMat], g.selectedMat, root, history);
            else
                drawProjectSettings();
            break;
        case AttrFocus::Tag:
            if (selected && selected->materialIndex >= 0) {
                drawTagAttributes(*selected, materials, root, history);
                break;
            }
            [[fallthrough]];
        case AttrFocus::Object:
            // Sin seleccion: Configuracion del Proyecto (seccion 15), nunca un panel vacio.
            if (selected) drawObjectAttributes(*selected, root, history);
            else drawProjectSettings();
            break;
    }
    ImGui::End();
}

// --- Linea de Tiempo (secciones 1 y 14) ---------------------------------------------

void drawTimeline(ImVec2 pos, ImVec2 size) {
    beginPanel("##timeline", pos, size, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float rulerH = 22.0f;
    const float pad = 12.0f;
    const ImVec2 r0(pos.x + 4, pos.y + 3), r1(pos.x + size.x - 60, pos.y + 3 + rulerH);
    dl->AddRectFilled(r0, r1, kRulerBg);
    dl->AddRect(r0, r1, kBorderDark);

    const float x0 = r0.x + pad, x1 = r1.x - pad;
    const int span = std::max(1, g.maxFrame - g.minFrame);
    auto fx = [&](float f) { return x0 + (f - (float)g.minFrame) / (float)span * (x1 - x0); };
    const float pxPerFrame = (x1 - x0) / (float)span;
    int labelStep = 5;
    while (labelStep * pxPerFrame < 34.0f) labelStep *= 2;
    const int tickStep = pxPerFrame >= 5.0f ? 1 : std::max(1, labelStep / 5);

    // Rango de previo (franja algo mas clara)
    dl->AddRectFilled(ImVec2(fx((float)g.previewMin), r0.y + 1), ImVec2(fx((float)g.previewMax), r1.y - 1),
                      IM_COL32(64, 64, 64, 255));
    for (int f = g.minFrame; f <= g.maxFrame; f += tickStep) {
        const float x = fx((float)f);
        const bool major = (f % labelStep) == 0;
        dl->AddLine(ImVec2(x, r1.y - (major ? 9 : 5)), ImVec2(x, r1.y - 1), IM_COL32(150, 150, 150, 255));
        if (major) {
            char buf[16];
            std::snprintf(buf, sizeof(buf), "%d", f);
            dl->AddText(ImVec2(x + 2, r0.y + 1), IM_COL32(190, 190, 190, 255), buf);
        }
    }

    // Arrastrar en la regla mueve el fotograma actual.
    ImGui::SetCursorScreenPos(r0);
    ImGui::InvisibleButton("##ruler", ImVec2(r1.x - r0.x, r1.y - r0.y));
    if (ImGui::IsItemActive()) {
        const float t = (ImGui::GetIO().MousePos.x - x0) / std::max(1.0f, x1 - x0);
        g.frame = std::round(std::clamp((float)g.minFrame + t * (float)span, (float)g.minFrame, (float)g.maxFrame));
        g.playDir = 0;
    }
    if (ImGui::IsItemHovered()) g.hint = "Línea de Tiempo: haga clic y arrastre para cambiar el fotograma actual.";

    // Claves del objeto activo: cuadraditos sobre la regla, como en C4D.
    for (int k : g.selKeys) {
        if (k < g.minFrame || k > g.maxFrame) continue;
        const float x = fx((float)k);
        const float hw = std::max(2.0f, std::min(5.0f, pxPerFrame * 0.5f));
        dl->AddRectFilled(ImVec2(x - hw, r1.y - 9), ImVec2(x + hw, r1.y - 1), IM_COL32(110, 160, 230, 255));
        dl->AddRect(ImVec2(x - hw, r1.y - 9), ImVec2(x + hw, r1.y - 1), IM_COL32(20, 30, 50, 255));
    }

    // Cursor de tiempo
    {
        const int fi = (int)std::lround(g.frame);
        const float x = fx(g.frame);
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%d", fi);
        const ImVec2 ts = ImGui::CalcTextSize(buf);
        dl->AddRectFilled(ImVec2(x - ts.x * 0.5f - 4, r0.y + 1), ImVec2(x + ts.x * 0.5f + 4, r0.y + ts.y + 2), kPlayhead, 2.0f);
        dl->AddText(ImVec2(x - ts.x * 0.5f, r0.y + 1), IM_COL32(255, 255, 255, 255), buf);
        dl->AddLine(ImVec2(x, r0.y + ts.y + 2), ImVec2(x, r1.y - 1), kPlayhead, 2.0f);
        std::snprintf(buf, sizeof(buf), "%d F", fi);
        dl->AddText(ImVec2(r1.x + 8, r0.y + 3), kTextCol, buf);
    }

    // Fila de transporte
    ImGui::SetCursorScreenPos(ImVec2(pos.x + 6, r1.y + 4));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(3, 0));
    const float b = std::min(size.y, 56.0f) - rulerH - 12.0f; // alto fijo aunque el panel crezca (SFM)
    ImGui::SetNextItemWidth(56);
    ImGui::DragInt("##tmin", &g.minFrame, 0.2f, -100000, g.maxFrame - 1, "%d F");
    setHint("Tiempo Mínimo del proyecto.");
    ImGui::SameLine();
    {
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float w = std::max(40.0f, size.x * 0.22f);
        ImGui::InvisibleButton("##range", ImVec2(w, b));
        dl->AddRectFilled(ImVec2(p.x, p.y + b * 0.3f), ImVec2(p.x + w, p.y + b * 0.7f), IM_COL32(40, 40, 40, 255), 2);
        const float a0 = p.x + (float)(g.previewMin - g.minFrame) / (float)span * w;
        const float a1 = p.x + (float)(g.previewMax - g.minFrame) / (float)span * w;
        dl->AddRectFilled(ImVec2(a0, p.y + b * 0.3f), ImVec2(a1, p.y + b * 0.7f), IM_COL32(120, 120, 120, 255), 2);
        setHint("Rango de previo (Tiempo Mínimo/Máximo de Previo).");
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(56);
    ImGui::DragInt("##tmax", &g.maxFrame, 0.2f, g.minFrame + 1, 100000, "%d F");
    setHint("Tiempo Máximo del proyecto (editable, la regla se reescala).");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(56);
    ImGui::DragInt("##pmax2", &g.previewMax, 0.2f, g.previewMin, g.maxFrame, "%d F");
    setHint("Tiempo Máximo de Previo.");
    ImGui::SameLine(0, 14);

    auto tbtn = [&](const char* id, Icon icon, bool active, bool enabled, const char* tip) {
        const bool clicked = iconButton(id, icon, b, active, enabled, tip, tip) == BtnResult::Click;
        ImGui::SameLine();
        return clicked;
    };
    if (tbtn("tstart", Icon::ToStart, false, true, "Ir al Inicio [Mayús+F]")) g.frame = (float)g.minFrame;
    if (tbtn("tprevk", Icon::PrevKey, false, !g.selKeys.empty(), "Ir a la Clave Anterior [Ctrl+F]")) jumpToKey(-1);
    if (tbtn("tprev", Icon::StepBack, false, true, "Ir al Fotograma Anterior [F]")) g.frame = std::max((float)g.minFrame, std::ceil(g.frame) - 1);
    if (tbtn("tplayb", Icon::PlayBack, g.playDir == -1, true, "Reproducir Hacia Atrás [F6]")) g.playDir = g.playDir == -1 ? 0 : -1;
    if (tbtn("tplay", g.playDir == 1 ? Icon::Pause : Icon::Play, g.playDir == 1, true, "Reproducir Hacia Delante [F8]")) g.playDir = g.playDir == 1 ? 0 : 1;
    if (tbtn("tnext", Icon::StepFwd, false, true, "Ir al Fotograma Siguiente [G]")) g.frame = std::min((float)g.maxFrame, std::floor(g.frame) + 1);
    if (tbtn("tnextk", Icon::NextKey, false, !g.selKeys.empty(), "Ir a la Clave Siguiente [Ctrl+G]")) jumpToKey(1);
    if (tbtn("tend", Icon::ToEnd, false, true, "Ir al Final [Mayús+G]")) g.frame = (float)g.maxFrame;
    toolbarSeparator(true, b);
    ImGui::SameLine();
    if (tbtn("trec", Icon::Record, false, true, "Grabar Objetos Activos [F9]")) g.recordRequested = true;
    if (tbtn("tauto", Icon::AutoKey, g.autoKey, true, "Fotogramas Clave Automáticos [Ctrl+F9]")) g.autoKey = !g.autoKey;
    tbtn("tksel", Icon::KeySel, false, false, "Selección de Claves (todavía no implementado)");
    toolbarSeparator(true, b);
    ImGui::SameLine();
    if (tbtn("tkpos", Icon::KeyPos, g.keyPos, true, "Grabar Posición")) g.keyPos = !g.keyPos;
    if (tbtn("tkscl", Icon::KeyScale, g.keyScl, true, "Grabar Escala")) g.keyScl = !g.keyScl;
    if (tbtn("tkrot", Icon::KeyRot, g.keyRot, true, "Grabar Rotación")) g.keyRot = !g.keyRot;
    tbtn("tkpar", Icon::KeyParam, false, false, "Grabar Parámetro (todavía no implementado)");
    tbtn("tkpla", Icon::KeyPLA, false, false, "Grabar Nivel de Puntos (todavía no implementado)");
    ImGui::PopStyleVar();
    // Panel mas alto que la tira de transporte (SFM): el resto es donde ira
    // el editor de curvas; de momento se dice tal cual.
    if (size.y > 110.0f) {
        const char* msg = "Editor de Gráficos / Hoja de Claves: todavía no implementado en Iron3D";
        const ImVec2 ts = ImGui::CalcTextSize(msg);
        ImDrawList* tdl = ImGui::GetWindowDrawList();
        const float top = pos.y + 60.0f;
        tdl->AddLine(ImVec2(pos.x + 4, top), ImVec2(pos.x + size.x - 4, top), kBorderDark);
        tdl->AddText(ImVec2(pos.x + (size.x - ts.x) * 0.5f, top + (size.y - 60.0f - ts.y) * 0.5f), kTextDim, msg);
    }
    ImGui::End();
}

// --- Gestor de Materiales (secciones 9 y 19.1) --------------------------------------

void drawMaterialManager(SceneNode& root, SceneNode*& selected, SceneHistory& history,
                         std::vector<Material>& materials, ImVec2 pos, ImVec2 size) {
    beginPanel("##matmgr", pos, size, ImGuiWindowFlags_MenuBar);
    if (ImGui::BeginMenuBar()) {
        panelGrip(names().materials);
        if (ImGui::BeginMenu("Crear")) {
            ImGui::MenuItem("Nuevo Material PBR", "Ctrl+Mayús+N", false, false);
            if (ImGui::MenuItem("Nuevo Material", "Ctrl+N")) {
                history.commitBefore(root);
                newMaterial(materials);
            }
            ImGui::BeginMenu("Shader", false);
            ImGui::Separator();
            disabledItems({{"Cargar Materiales...", "Ctrl+Mayús+O"}, {"Guardar Materiales como...", ""},
                           {"Guardar Todos los Materiales como...", ""}, {nullptr, nullptr},
                           {"Cargar Material Preestablecido", ""}, {"Guardar Material Preestablecido...", ""}});
            ImGui::EndMenu();
        }
        const bool hasSel = g.selectedMat >= 0 && (size_t)g.selectedMat < materials.size();
        if (ImGui::BeginMenu("Edición")) {
            ImGui::MenuItem("Deshacer", "Ctrl+Z", false, false);
            ImGui::Separator();
            if (ImGui::MenuItem("Eliminar", "Supr", false, hasSel)) {
                history.commitBefore(root);
                fixupMaterialIndicesAfterRemoval(root, g.selectedMat);
                materials.erase(materials.begin() + g.selectedMat);
                g.selectedMat = -1;
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Función")) {
            if (ImGui::MenuItem("Editar...", nullptr, false, hasSel)) g.showMatEditor = true;
            if (ImGui::MenuItem("Renombrar", nullptr, false, hasSel)) {
                g.renamingMat = g.selectedMat;
                std::snprintf(g.renameBuf, sizeof(g.renameBuf), "%s", materials[(size_t)g.selectedMat].name.c_str());
                g.renameFocus = true;
            }
            if (ImGui::MenuItem("Aplicar", nullptr, false, hasSel && selected && hasGeometry(selected->primitive))) {
                history.commitBefore(root);
                selected->materialIndex = g.selectedMat;
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Eliminar Materiales sin Usar", nullptr, false, !materials.empty())) {
                history.commitBefore(root);
                for (int i = (int)materials.size() - 1; i >= 0; --i) {
                    if (materialUsed(root, i)) continue;
                    fixupMaterialIndicesAfterRemoval(root, i);
                    materials.erase(materials.begin() + i);
                }
                g.selectedMat = -1;
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Textura")) {
            disabledItems({{"Cargar Textura...", ""}, {"Editar Textura...", ""}});
            ImGui::EndMenu();
        }
        ImGui::EndMenuBar();
    }

    ImGui::BeginChild("##swatches", ImVec2(0, 0));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float cellW = 64.0f, cellH = 70.0f;
    const float availW = ImGui::GetContentRegionAvail().x;
    const int perRow = std::max(1, (int)(availW / (cellW + 4)));
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    int pendingDelete = -1;
    for (int i = 0; i < (int)materials.size(); ++i) {
        Material& m = materials[(size_t)i];
        const ImVec2 p(origin.x + (i % perRow) * (cellW + 4), origin.y + (i / perRow) * (cellH + 4));
        ImGui::SetCursorScreenPos(p);
        ImGui::PushID(i);
        ImGui::InvisibleButton("##sw", ImVec2(cellW, cellH));
        const bool hov = ImGui::IsItemHovered();
        if (ImGui::IsItemClicked() || ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
            g.selectedMat = i;
            g.focus = AttrFocus::Material;
        }
        if (hov && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            const bool onName = ImGui::GetIO().MousePos.y > p.y + cellH - 18;
            if (onName) {
                g.renamingMat = i;
                std::snprintf(g.renameBuf, sizeof(g.renameBuf), "%s", m.name.c_str());
                g.renameFocus = true;
            } else {
                g.showMatEditor = true;
            }
        }
        if (hov) g.hint = "Clic: seleccionar material. Doble clic: Editor de Material (en el nombre: renombrar). Arrastre a un objeto para asignarlo.";
        if (ImGui::BeginDragDropSource()) {
            ImGui::SetDragDropPayload("IRON3D_MAT", &i, sizeof(int));
            float previewSpec, previewShin;
            materialBallSpecShininess(m, previewSpec, previewShin);
            icons::materialBall(ImGui::GetForegroundDrawList(), ImVec2(ImGui::GetIO().MousePos.x + 18, ImGui::GetIO().MousePos.y + 18),
                                14, ImVec4(m.baseColor.x, m.baseColor.y, m.baseColor.z, 1), previewSpec, previewShin);
            ImGui::Text("%s", m.name.c_str());
            ImGui::EndDragDropSource();
        }
        if (ImGui::BeginPopupContextItem("##mctx")) {
            if (ImGui::MenuItem("Editar...")) g.showMatEditor = true;
            if (ImGui::MenuItem("Aplicar", nullptr, false, selected && hasGeometry(selected->primitive))) {
                history.commitBefore(root);
                selected->materialIndex = i;
            }
            if (ImGui::MenuItem("Renombrar")) {
                g.renamingMat = i;
                std::snprintf(g.renameBuf, sizeof(g.renameBuf), "%s", m.name.c_str());
                g.renameFocus = true;
            }
            if (ImGui::MenuItem("Eliminar", "Supr")) pendingDelete = i;
            ImGui::EndPopup();
        }

        const bool sel = g.selectedMat == i;
        if (hov) dl->AddRectFilled(p, ImVec2(p.x + cellW, p.y + cellH), IM_COL32(255, 255, 255, 12), 2);
        float previewSpec, previewShin;
        materialBallSpecShininess(m, previewSpec, previewShin);
        icons::materialBall(dl, ImVec2(p.x + cellW * 0.5f, p.y + 26), 23, ImVec4(m.baseColor.x, m.baseColor.y, m.baseColor.z, 1),
                            previewSpec, previewShin);
        const ImVec2 n0(p.x + 1, p.y + cellH - 17), n1(p.x + cellW - 1, p.y + cellH - 1);
        if (g.renamingMat == i) {
            ImGui::SetCursorScreenPos(n0);
            ImGui::SetNextItemWidth(cellW - 2);
            if (g.renameFocus) {
                ImGui::SetKeyboardFocusHere();
                g.renameFocus = false;
            }
            const bool done = ImGui::InputText("##mren", g.renameBuf, sizeof(g.renameBuf),
                                               ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
            if (done || ImGui::IsItemDeactivated()) {
                if (g.renameBuf[0]) {
                    history.commitBefore(root);
                    m.name = g.renameBuf;
                }
                g.renamingMat = -1;
            }
        } else {
            if (sel) dl->AddRectFilled(n0, n1, kAmber, 2);
            dl->PushClipRect(n0, n1, true);
            const ImVec2 ts = ImGui::CalcTextSize(m.name.c_str());
            dl->AddText(ImVec2(p.x + std::max(2.0f, (cellW - ts.x) * 0.5f), n0.y + 1),
                        sel ? IM_COL32(25, 25, 25, 255) : kTextCol, m.name.c_str());
            dl->PopClipRect();
        }
        ImGui::PopID();
    }
    const int rowsUsed = materials.empty() ? 0 : ((int)materials.size() + perRow - 1) / perRow;
    ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + rowsUsed * (cellH + 4)));
    const ImVec2 rest = ImGui::GetContentRegionAvail();
    if (rest.y > 4 && rest.x > 4) {
        // Doble clic en el hueco vacio crea un material nuevo, como en C4D.
        ImGui::InvisibleButton("##matempty", rest);
        // Primero el clic simple (deselecciona) y despues el doble clic, que
        // llega en el mismo frame que su segundo clic: asi el material nuevo
        // queda seleccionado.
        if (ImGui::IsItemClicked()) g.selectedMat = -1;
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            history.commitBefore(root);
            newMaterial(materials);
        }
        if (ImGui::IsItemHovered()) g.hint = "Doble clic en un espacio vacío del Gestor de Materiales para crear un material nuevo.";
        if (materials.empty()) {
            const char* t = "Doble clic aquí para crear un material";
            const ImVec2 ts = ImGui::CalcTextSize(t);
            const ImVec2 c(ImGui::GetItemRectMin().x + (rest.x - ts.x) * 0.5f, ImGui::GetItemRectMin().y + (rest.y - ts.y) * 0.5f);
            dl->AddText(c, IM_COL32(110, 110, 110, 255), t);
        }
    }
    ImGui::EndChild();
    ImGui::End();

    if (pendingDelete >= 0) {
        history.commitBefore(root);
        fixupMaterialIndicesAfterRemoval(root, pendingDelete);
        materials.erase(materials.begin() + pendingDelete);
        g.selectedMat = -1;
    }
}

// --- Gestor de Coordenadas (seccion 1) -----------------------------------------------

void drawCoordinatesManager(SceneNode& root, SceneNode* selected, SceneHistory& history, ImVec2 pos, ImVec2 size) {
    beginPanel("##coordmgr", pos, size, ImGuiWindowFlags_NoScrollbar);
    // Con ficha de nombre (presets que no son C4D) la ficha va en su propia
    // linea y las cabeceras debajo, para no desplazar las columnas.
    const bool chipLine = !isC4DArrangement();
    const float startX = ImGui::GetCursorPosX();
    panelGrip(chipLine ? names().coords : nullptr);
    const float colW = (ImGui::GetContentRegionAvail().x - 6) / 3.0f;
    const char* headers[] = {"Posición", "Tamaño", "Rotación"};
    if (g.coordSizeMode == 1) headers[1] = "Escala";
    if (!chipLine) ImGui::SameLine();
    const float baseX = startX + 12.0f + ImGui::GetStyle().ItemSpacing.x - 14.0f;
    for (int c = 0; c < 3; ++c) {
        const float w = ImGui::CalcTextSize(headers[c]).x;
        if (c == 0 && chipLine) ImGui::SetCursorPosX(baseX + (colW - w) * 0.5f);
        else ImGui::SameLine(baseX + c * colW + (colW - w) * 0.5f);
        if (g_fontBold) ImGui::PushFont(g_fontBold);
        ImGui::TextUnformatted(headers[c]);
        if (g_fontBold) ImGui::PopFont();
    }
    ImGui::Separator();

    if (!selected) ImGui::BeginDisabled();
    glm::vec3 dummy(0.0f);
    glm::vec3& posRef = selected ? selected->position : dummy;
    glm::vec3& rotRef = selected ? selected->rotationDeg : dummy;
    glm::vec3& sclRef = selected ? selected->scale : dummy;
    const glm::vec3 base = selected ? baseSizeCm(*selected) : glm::vec3(0.0f);
    const char* axisP[] = {"X", "Y", "Z"};
    const char* axisR[] = {"H", "P", "B"};
    const int rotIdx[] = {1, 0, 2};
    for (int i = 0; i < 3; ++i) {
        ImGui::PushID(i);
        for (int c = 0; c < 3; ++c) {
            if (c > 0) ImGui::SameLine(baseX + c * colW + 14);
            else ImGui::SetCursorPosX(baseX + 14);
            ImGui::TextUnformatted(c == 2 ? axisR[i] : axisP[i]);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(colW - 34);
            ImGui::PushID(c);
            if (!selected) {
                // Sin seleccion C4D deja los campos vacios con "--".
                float none = 0.0f;
                ImGui::DragFloat("##v", &none, 1.0f, 0, 0, "--");
            } else if (c == 0) {
                float cm = posRef[i] * kCmPerUnit;
                if (ImGui::DragFloat("##v", &cm, 1.0f, 0, 0, "%.0f cm")) posRef[i] = cm / kCmPerUnit;
            } else if (c == 1) {
                if (g.coordSizeMode == 0 && base[i] > 0.0f) {
                    float sz = sclRef[i] * base[i];
                    if (ImGui::DragFloat("##v", &sz, 1.0f, 0.1f, 1.0e6f, "%.0f cm")) sclRef[i] = std::max(0.0005f, sz / base[i]);
                } else if (g.coordSizeMode == 0) {
                    float zero = 0.0f;
                    ImGui::BeginDisabled();
                    ImGui::DragFloat("##v", &zero, 1.0f, 0, 0, "%.0f cm");
                    ImGui::EndDisabled();
                } else {
                    ImGui::DragFloat("##v", &sclRef[i], 0.01f, 0.001f, 1000.0f, "%.3f");
                }
            } else {
                ImGui::DragFloat("##v", &rotRef[rotIdx[i]], 0.5f, 0, 0, "%.1f °");
            }
            if (selected) trackEdit(history, root);
            ImGui::PopID();
        }
        ImGui::PopID();
    }
    if (!selected) ImGui::EndDisabled();

    ImGui::Dummy(ImVec2(1, 2));
    ImGui::SetCursorPosX(baseX + 14);
    ImGui::SetNextItemWidth(colW - 14);
    const char* spaces[] = {"Objeto (Rel)", "Objeto (Abs)", "Mundo"};
    if (ImGui::BeginCombo("##space", spaces[g.coordSpace])) {
        for (int i = 0; i < 3; ++i) {
            if (i > 0) ImGui::BeginDisabled();
            if (ImGui::Selectable(spaces[i], g.coordSpace == i)) g.coordSpace = i;
            if (i > 0) ImGui::EndDisabled();
        }
        ImGui::EndCombo();
    }
    setHint("Sistema de coordenadas de los campos (solo Objeto (Rel) disponible por ahora).");
    ImGui::SameLine(baseX + colW + 14);
    ImGui::SetNextItemWidth(colW - 14);
    const char* sizeModes[] = {"Tamaño", "Escala"};
    ImGui::Combo("##sizemode", &g.coordSizeMode, sizeModes, 2);
    setHint("Tamaño: medidas en cm. Escala: multiplicador del objeto.");
    ImGui::SameLine(baseX + 2 * colW + 14);
    if (!selected) ImGui::BeginDisabled();
    if (ImGui::Button("Aplicar", ImVec2(colW - 14, 0)) && selected) history.commitBefore(root);
    if (!selected) ImGui::EndDisabled();
    setHint("Aplicar los valores de los campos al objeto seleccionado.");
    ImGui::End();
}

// --- Barra de estado -----------------------------------------------------------------------

void drawStatusBar(float framerate, GizmoMode mode, const ViewportState& vp, RendererBackend backend, ImVec2 pos,
                   ImVec2 size) {
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImGui::ColorConvertU32ToFloat4(kStatusBg));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 2));
    beginPanel("##status", pos, size, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoInputs);
    std::string text = g.hint;
    if (text.empty() && ImGui::GetTime() < g.statusUntil) text = g.status;
    if (text.empty()) {
        if (vp.selectTool && vp.selectShape == 1)
            text = "Selección Rectangular: arrastre para seleccionar los objetos que toque el rectángulo. MAYÚSCULAS añade, CTRL quita.";
        else if (vp.selectTool && vp.selectShape == 2)
            text = "Selección con Lazo: dibuje una zona para seleccionar los objetos que queden dentro. MAYÚSCULAS añade, CTRL quita.";
        else if (vp.selectTool) text = "Selección en Vivo: Haga clic para seleccionar elementos.";
        else if (mode == GizmoMode::Translate)
            text = "Mover: Haga clic y arrastre para mover elementos. Pulse MAYÚSCULAS para cuantificar el movimiento / añadir a la selección en el modo punto, CTRL para eliminar.";
        else if (mode == GizmoMode::Rotate)
            text = "Rotar: Haga clic y arrastre para rotar elementos. Pulse MAYÚSCULAS para cuantificar la rotación / añadir a la selección en el modo punto, CTRL para eliminar.";
        else
            text = "Escalar: Haga clic y arrastre para escalar elementos. Pulse MAYÚSCULAS para cuantificar el escalado / añadir a la selección en el modo punto, CTRL para eliminar.";
    }
    ImGui::TextColored(ImVec4(0.78f, 0.78f, 0.78f, 1), "%s", text.c_str());
    char right[64];
    std::snprintf(right, sizeof(right), "%s  ·  %.0f FPS", backend == RendererBackend::Vulkan ? "Vulkan" : "OpenGL", framerate);
    const float w = ImGui::CalcTextSize(right).x;
    ImGui::SameLine(size.x - w - 10);
    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1), "%s", right);
    ImGui::End();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}

// --- Dialogos flotantes ----------------------------------------------------------------------

void drawSceneDialog(SceneNode& root, SceneNode*& selected, const std::filesystem::path& scenesDir,
                     SceneHistory& history, std::vector<Material>& materials) {
    if (!g.showScene) return;
    static char filenameBuf[128] = "escena";
    static std::string statusMsg;
    static bool statusIsError = false;
    ImGui::SetNextWindowSize(ImVec2(420, 0), ImGuiCond_Appearing);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::Begin(g.sceneLoadMode ? "Abrir###scenedlg" : "Guardar como###scenedlg", &g.showScene,
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Nombre de archivo:");
        ImGui::SetNextItemWidth(300);
        ImGui::InputText("##fn", filenameBuf, sizeof(filenameBuf));
        ImGui::SameLine();
        ImGui::TextDisabled(".iron");
        ImGui::TextDisabled("Carpeta: %s", scenesDir.string().c_str());
        ImGui::Separator();
        const std::filesystem::path path = scenesDir / (std::string(filenameBuf) + ".iron");
        if (ImGui::Button(g.sceneLoadMode ? "Abrir" : "Guardar", ImVec2(120, 0))) {
            std::string err;
            if (g.sceneLoadMode) {
                if (loadScene(path, root, materials, &err)) {
                    selected = nullptr;
                    history.reset();
                    g.selectedMat = -1;
                    statusMsg = "Cargado: " + path.filename().string();
                    statusIsError = false;
                } else {
                    statusMsg = "Error al cargar: " + err;
                    statusIsError = true;
                }
            } else if (saveScene(path, root, materials, &err)) {
                statusMsg = "Guardado: " + path.filename().string();
                statusIsError = false;
            } else {
                statusMsg = "Error al guardar: " + err;
                statusIsError = true;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancelar", ImVec2(120, 0))) g.showScene = false;
        if (!statusMsg.empty())
            ImGui::TextColored(statusIsError ? ImVec4(0.95f, 0.35f, 0.35f, 1) : ImVec4(0.4f, 0.9f, 0.4f, 1), "%s", statusMsg.c_str());
    }
    ImGui::End();
}

// --- Preferencias > Interfaz: presets de interfaz -----------------------------

ImU32 seedU32(const glm::vec3& c, float delta = 0.0f) {
    const glm::vec3 v = glm::clamp(c + glm::vec3(delta), glm::vec3(0.0f), glm::vec3(1.0f)) * 255.0f;
    return IM_COL32((int)(v.x + 0.5f), (int)(v.y + 0.5f), (int)(v.z + 0.5f), 255);
}

// Miniatura de un preset: el mismo computeLayout que usa la ventana, escalado
// a la tarjeta y pintado con los colores del preset.
void drawPresetThumb(ImDrawList* dl, ImVec2 a, ImVec2 sz, const UiPreset& p) {
    const ImVec2 ref(1600.0f, 900.0f);
    const LayoutRects r = computeLayout(p.layout, ImVec2(0, 0), ref, 26.0f, 22.0f);
    const float sx = sz.x / ref.x, sy = sz.y / ref.y;
    const glm::vec3& bg = p.theme.panelBg;
    const ImU32 border = seedU32(bg, -0.12f);
    dl->AddRectFilled(a, ImVec2(a.x + sz.x, a.y + sz.y), seedU32(bg, -0.03f));
    auto box = [&](const PanelRect& pr, ImU32 col) {
        if (!pr.visible()) return;
        const ImVec2 p0(a.x + pr.pos.x * sx, a.y + pr.pos.y * sy);
        const ImVec2 p1(p0.x + pr.size.x * sx, p0.y + pr.size.y * sy);
        dl->AddRectFilled(p0, p1, col);
        dl->AddRect(p0, p1, border);
    };
    box(r.top, seedU32(bg, 0.02f));
    box(r.left, seedU32(bg));
    box(r.viewport, seedU32(p.theme.viewportBg));
    box(r.objects, seedU32(bg));
    box(r.attributes, seedU32(bg));
    box(r.objTabs, seedU32(bg, -0.05f));
    box(r.attrTabs, seedU32(bg, -0.05f));
    box(r.timeline, seedU32(bg, -0.04f));
    box(r.materials, seedU32(bg));
    box(r.coords, seedU32(bg));
    box(r.status, seedU32(bg, -0.13f));
    // Detalles: fila seleccionada en Objetos (acento), objeto en el visor
    // (seleccion) y cabezal en la linea de tiempo.
    if (r.objects.visible()) {
        const float y = a.y + (r.objects.pos.y + 60.0f) * sy;
        dl->AddRectFilled(ImVec2(a.x + r.objects.pos.x * sx + 2, y),
                          ImVec2(a.x + (r.objects.pos.x + r.objects.size.x) * sx - 2, y + 2.0f), seedU32(p.theme.toolBlue));
    }
    if (r.viewport.visible()) {
        const ImVec2 c(a.x + (r.viewport.pos.x + r.viewport.size.x * 0.5f) * sx,
                       a.y + (r.viewport.pos.y + r.viewport.size.y * 0.55f) * sy);
        const float h = std::max(3.0f, r.viewport.size.y * sy * 0.14f);
        dl->AddRectFilled(ImVec2(c.x - h, c.y - h), ImVec2(c.x + h, c.y + h), seedU32(glm::vec3(0.62f)));
        dl->AddRect(ImVec2(c.x - h, c.y - h), ImVec2(c.x + h, c.y + h), seedU32(p.theme.selection));
    }
    if (r.timeline.visible()) {
        const float x = a.x + (r.timeline.pos.x + r.timeline.size.x * 0.3f) * sx;
        dl->AddLine(ImVec2(x, a.y + r.timeline.pos.y * sy), ImVec2(x, a.y + (r.timeline.pos.y + r.timeline.size.y) * sy),
                    seedU32(p.theme.accent), 1.5f);
    }
}

bool presetCard(const char* id, const UiPreset& p, bool active, float w) {
    const float thumbH = std::floor(w * 9.0f / 16.0f);
    const float h = thumbH + ImGui::GetTextLineHeight() + 10.0f;
    ImGui::PushID(id);
    const ImVec2 a = ImGui::GetCursorScreenPos();
    const bool clicked = ImGui::InvisibleButton("##card", ImVec2(w, h));
    const bool hov = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(a, ImVec2(a.x + w, a.y + h), hov ? kTabOn : kTabOff, 3.0f);
    drawPresetThumb(dl, ImVec2(a.x + 4, a.y + 4), ImVec2(w - 8, thumbH - 4), p);
    const ImVec2 ts = ImGui::CalcTextSize(p.name.c_str());
    dl->PushClipRect(a, ImVec2(a.x + w, a.y + h), true);
    dl->AddText(ImVec2(a.x + std::max(4.0f, (w - ts.x) * 0.5f), a.y + thumbH + 4), active ? kAmber : kTextCol, p.name.c_str());
    dl->PopClipRect();
    if (active) dl->AddRect(a, ImVec2(a.x + w, a.y + h), kAmber, 3.0f, 0, 2.0f);
    if (hov) ImGui::SetTooltip("%s\nDisposición: %s", p.name.c_str(), arrangementName(p.layout.arrangement));
    ImGui::PopID();
    return clicked;
}

void drawInterfacePage(Preferences& prefs, const std::filesystem::path& prefsPath, std::string& statusMsg) {
    UiPreset& cur = prefs.uiPreset;
    const std::filesystem::path presetsDir = prefsPath.parent_path() / "presets";
    static std::vector<UiPreset> userPresets;
    static bool userLoaded = false;
    if (!userLoaded) {
        userPresets = loadUserPresets(presetsDir);
        userLoaded = true;
    }
    auto choose = [&](const UiPreset& p) {
        cur = p;
        applyPreset(cur);
        statusMsg.clear();
    };

    ImGui::TextDisabled("Colores y disposición de los paneles como en cada programa. Se aplica al momento.");
    ImGui::Dummy(ImVec2(0, 2));

    // Galeria: integrados y, debajo, los del usuario.
    const float cardW = 132.0f, gap = 8.0f;
    const int perRow = std::max(1, (int)((ImGui::GetContentRegionAvail().x + gap) / (cardW + gap)));
    int col = 0;
    auto place = [&]() {
        if (col > 0) ImGui::SameLine(0, gap);
        col = (col + 1) % perRow;
    };
    for (const UiPreset& p : builtinPresets()) {
        place();
        if (presetCard(p.id.c_str(), p, cur.id == p.id, cardW)) choose(p);
    }
    if (cur.id == "custom") {
        place();
        presetCard("custom", cur, true, cardW);
    }
    if (!userPresets.empty()) {
        ImGui::Dummy(ImVec2(0, 4));
        ImGui::TextDisabled("Mis presets");
        col = 0;
        for (const UiPreset& p : userPresets) {
            place();
            if (presetCard(p.id.c_str(), p, cur.id == p.id, cardW)) choose(p);
        }
    }

    ImGui::Dummy(ImVec2(0, 6));
    sectionHeader("Personalizar");
    bool changed = false;
    // Disposicion base: cambiarla trae la colocacion y las metricas de ese
    // programa, pero conserva los colores que haya.
    int arr = (int)cur.layout.arrangement;
    const char* arrNames[] = {"Cinema 4D", "Blender", "Maya", "3ds Max", "Source Filmmaker"};
    ImGui::SetNextItemWidth(220);
    if (ImGui::Combo("Disposición", &arr, arrNames, IM_ARRAYSIZE(arrNames))) {
        const UiPreset& base = builtinPresets()[(size_t)arr];
        cur.layout = base.layout;
        cur.style = base.style;
        changed = true;
    }
    if (ImGui::BeginTable("##cust", 2, ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableNextColumn();
        ImGui::TextDisabled("Colores");
        changed |= ImGui::ColorEdit3("Fondo de paneles", &cur.theme.panelBg.x, ImGuiColorEditFlags_NoInputs);
        changed |= ImGui::ColorEdit3("Fondo de campos", &cur.theme.fieldBg.x, ImGuiColorEditFlags_NoInputs);
        changed |= ImGui::ColorEdit3("Acento", &cur.theme.accent.x, ImGuiColorEditFlags_NoInputs);
        changed |= ImGui::ColorEdit3("Azul de herramientas", &cur.theme.toolBlue.x, ImGuiColorEditFlags_NoInputs);
        changed |= ImGui::ColorEdit3("Texto", &cur.theme.text.x, ImGuiColorEditFlags_NoInputs);
        changed |= ImGui::ColorEdit3("Selección", &cur.theme.selection.x, ImGuiColorEditFlags_NoInputs);
        changed |= ImGui::ColorEdit3("Fondo del visor", &cur.theme.viewportBg.x, ImGuiColorEditFlags_NoInputs);
        ImGui::TableNextColumn();
        ImGui::TextDisabled("Paneles");
        LayoutSpec& l = cur.layout;
        const float sw = std::max(80.0f, ImGui::GetContentRegionAvail().x - 150.0f);
        ImGui::SetNextItemWidth(sw);
        changed |= ImGui::SliderFloat("Barra superior", &l.toolbarH, 26.0f, 48.0f, "%.0f px");
        ImGui::SetNextItemWidth(sw);
        if (ImGui::SliderFloat("Columna lateral", &l.sideFrac, 0.15f, 0.40f, "%.2f")) {
            l.sideMin = std::min(l.sideMin, 220.0f); // que el minimo no anule el deslizador
            changed = true;
        }
        if (l.arrangement == Arrangement::Maya || l.arrangement == Arrangement::Max) {
            ImGui::SetNextItemWidth(sw);
            changed |= ImGui::SliderFloat("Columna de objetos", &l.outlinerW, 150.0f, 360.0f, "%.0f px");
        } else {
            ImGui::SetNextItemWidth(sw);
            changed |= ImGui::SliderFloat("Objetos / Atributos", &l.objFrac, 0.20f, 0.80f, "%.2f");
        }
        ImGui::SetNextItemWidth(sw);
        changed |= ImGui::SliderFloat("Fila inferior", &l.shelfFrac, 0.08f, 0.25f, "%.2f");
        ImGui::SetNextItemWidth(sw);
        if (l.timelineFrac > 0.0f) changed |= ImGui::SliderFloat("Línea de tiempo", &l.timelineFrac, 0.12f, 0.50f, "%.2f");
        else changed |= ImGui::SliderFloat("Línea de tiempo", &l.timelineH, 36.0f, 96.0f, "%.0f px");
        changed |= ImGui::Checkbox("Pestañas verticales", &l.verticalTabs);
        ImGui::SetNextItemWidth(sw);
        changed |= ImGui::SliderFloat("Redondeo", &cur.style.frameRounding, 0.0f, 8.0f, "%.0f px");
        if (changed) {
            cur.style.grabRounding = cur.style.frameRounding;
            cur.style.popupRounding = std::max(cur.style.popupRounding, 0.0f);
        }
        ImGui::EndTable();
    }
    if (changed) {
        // Tocar un preset lo convierte en uno propio sin guardar.
        cur.id = "custom";
        cur.name = "Personalizado (sin guardar)";
        applyPreset(cur);
        statusMsg.clear();
    }

    ImGui::Dummy(ImVec2(0, 4));
    static char newName[64] = "";
    ImGui::SetNextItemWidth(200);
    ImGui::InputTextWithHint("##pname", "Nombre del preset", newName, sizeof(newName));
    ImGui::SameLine();
    if (ImGui::Button("Guardar como preset")) {
        UiPreset p = cur;
        p.name = newName;
        if (saveUserPreset(presetsDir, p)) {
            userPresets = loadUserPresets(presetsDir);
            for (const UiPreset& u : userPresets)
                if (u.name == p.name || u.id == "user:" + std::string(newName)) cur = u;
            statusMsg = "Preset guardado en presets/.";
            newName[0] = 0;
        } else {
            statusMsg = "Escriba un nombre válido (letras, números, espacios, - o _).";
        }
    }
    const bool isUser = cur.id.rfind("user:", 0) == 0;
    ImGui::SameLine();
    if (!isUser) ImGui::BeginDisabled();
    if (ImGui::Button("Eliminar")) {
        if (deleteUserPreset(presetsDir, cur.name)) {
            userPresets = loadUserPresets(presetsDir);
            choose(builtinPresets()[(size_t)cur.layout.arrangement]);
            statusMsg = "Preset eliminado.";
        }
    }
    if (!isUser) ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Restablecer")) choose(builtinPresets()[(size_t)cur.layout.arrangement]);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Vuelve al preset integrado de %s.", arrangementName(cur.layout.arrangement));

    ImGui::Dummy(ImVec2(0, 4));
    if (ImGui::Button("Guardar##prefsui", ImVec2(120, 0))) {
        statusMsg = savePreferences(prefsPath, prefs) ? "Guardado. Se usará al abrir Iron3D."
                                                      : "Error al guardar las preferencias.";
    }
    ImGui::SameLine();
    ImGui::TextDisabled("Preset activo: %s", cur.name.c_str());
    if (!statusMsg.empty()) ImGui::TextUnformatted(statusMsg.c_str());
}

void drawPreferencesDialog(RendererBackend activeBackend, Preferences& prefs, const std::filesystem::path& prefsPath) {
    if (!g.showPrefs) return;
    static std::string statusMsg;
    ImGui::SetNextWindowSize(ImVec2(900, 640), ImGuiCond_Appearing);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::Begin("Preferencias", &g.showPrefs, ImGuiWindowFlags_NoCollapse)) {
        static int page = 0;
        const char* pages[] = {"Interfaz", "Visor", "Archivos", "Unidades", "Memoria", "Renderizador"};
        ImGui::BeginChild("##pages", ImVec2(150, 0), ImGuiChildFlags_Border);
        for (int i = 0; i < IM_ARRAYSIZE(pages); ++i) {
            if (i != 0 && i != 1 && i != 5) ImGui::BeginDisabled();
            if (ImGui::Selectable(pages[i], page == i)) page = i;
            if (i != 0 && i != 1 && i != 5) ImGui::EndDisabled();
        }
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("##page");
        sectionHeader(pages[page]);
        if (page == 0) {
            drawInterfacePage(prefs, prefsPath, statusMsg);
        } else if (page == 1 || page == 5) {

            ImGui::Text("Backend activo en esta sesión: %s", activeBackend == RendererBackend::Vulkan ? "Vulkan" : "OpenGL");
            ImGui::TextUnformatted("Backend de render (se aplica al reiniciar):");
            int idx = prefs.renderBackend == RendererBackend::Vulkan ? 0 : 1;
            if (ImGui::RadioButton("Vulkan", idx == 0)) prefs.renderBackend = RendererBackend::Vulkan;
            ImGui::SameLine();
            if (ImGui::RadioButton("OpenGL", idx == 1)) prefs.renderBackend = RendererBackend::OpenGL;
            if (ImGui::Button("Guardar")) {
                statusMsg = savePreferences(prefsPath, prefs) ? "Guardado. Reinicie Iron3D Studio para aplicarlo."
                                                              : "Error al guardar las preferencias.";
            }
            if (!statusMsg.empty()) ImGui::TextUnformatted(statusMsg.c_str());
        }
        ImGui::EndChild();
    }
    ImGui::End();
}

void drawMaterialEditor(SceneNode& root, SceneHistory& history, std::vector<Material>& materials) {
    if (!g.showMatEditor) return;
    if (g.selectedMat < 0 || (size_t)g.selectedMat >= materials.size()) {
        g.showMatEditor = false;
        return;
    }
    Material& m = materials[(size_t)g.selectedMat];
    ImGui::SetNextWindowSize(ImVec2(640, 460), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
    char title[160];
    std::snprintf(title, sizeof(title), "Editor de Material  [%s]###mateditor", m.name.c_str());
    if (ImGui::Begin(title, &g.showMatEditor, ImGuiWindowFlags_NoCollapse)) {
        ImGui::BeginChild("##left", ImVec2(190, 0), ImGuiChildFlags_Border);
        const ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddRectFilled(p, ImVec2(p.x + 170, p.y + 150), IM_COL32(30, 30, 30, 255));
        float previewSpec, previewShin;
        materialBallSpecShininess(m, previewSpec, previewShin);
        icons::materialBall(ImGui::GetWindowDrawList(), ImVec2(p.x + 85, p.y + 75), 62,
                            ImVec4(m.baseColor.x, m.baseColor.y, m.baseColor.z, 1), previewSpec, previewShin);
        ImGui::Dummy(ImVec2(170, 154));
        char nameBuf[128];
        std::snprintf(nameBuf, sizeof(nameBuf), "%s", m.name.c_str());
        ImGui::SetNextItemWidth(170);
        if (ImGui::InputText("##mename", nameBuf, sizeof(nameBuf)) && nameBuf[0]) m.name = nameBuf;
        trackEdit(history, root);
        ImGui::Separator();
        struct Ch { const char* name; bool on; bool usable; };
        const Ch channels[] = {{"Color", true, true}, {"Difusión", false, false}, {"Luminancia", false, false},
                               {"Transparencia", false, false}, {"Reflectancia", true, true}, {"Entorno", false, false},
                               {"Niebla", false, false}, {"Relieve", false, false}, {"Normales", false, false},
                               {"Alfa", false, false}, {"Fosforescencia", false, false},
                               {"Desplazamiento", false, false}, {nullptr, false, false}, {"Editor", false, false},
                               {"Iluminación", false, false}, {"Asignación", false, true}};
        for (int i = 0; i < IM_ARRAYSIZE(channels); ++i) {
            if (!channels[i].name) {
                ImGui::Separator();
                continue;
            }
            ImGui::PushID(i);
            bool on = channels[i].on;
            ImGui::BeginDisabled();
            ImGui::Checkbox("##on", &on);
            ImGui::EndDisabled();
            ImGui::SameLine();
            const bool act = g.matEditorChannel == i;
            if (act) ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(kAmber));
            if (!channels[i].usable) ImGui::BeginDisabled();
            if (ImGui::Selectable(channels[i].name, act)) g.matEditorChannel = i;
            if (!channels[i].usable) ImGui::EndDisabled();
            if (act) ImGui::PopStyleColor();
            ImGui::PopID();
        }
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("##right");
        g_labelW = 150.0f;
        if (g.matEditorChannel == 0) {
            sectionHeader("Color");
            ImGui::ColorPicker3("##picker", &m.baseColor.x,
                                ImGuiColorEditFlags_PickerHueBar | ImGuiColorEditFlags_DisplayHSV | ImGuiColorEditFlags_NoSidePreview);
            trackEdit(history, root);
            roFloat("Brillo", 100.0f, "%.0f %%");
            roCombo("Modelo", "Lambertiano");
        } else if (g.matEditorChannel == 4) {
            sectionHeader("Reflectancia — PBR (Metallic/Roughness)");
            ImGui::TextDisabled("  Capas:  GGX (Cook-Torrance)");
            propLabel("Metálico");
            float metallicPct = m.metallic * 100.0f;
            ImGui::SetNextItemWidth(fieldWidth(220));
            if (ImGui::SliderFloat("##metallic", &metallicPct, 0, 100, "%.0f %%")) m.metallic = metallicPct / 100.0f;
            trackEdit(history, root);
            propLabel("Rugosidad");
            float roughnessPct = m.roughness * 100.0f;
            ImGui::SetNextItemWidth(fieldWidth(220));
            if (ImGui::SliderFloat("##roughness", &roughnessPct, 0, 100, "%.0f %%")) m.roughness = roughnessPct / 100.0f;
            trackEdit(history, root);
        } else {
            sectionHeader("Asignación");
            std::vector<SceneNode*> users;
            collectUsers(root, g.selectedMat, users);
            if (users.empty()) ImGui::TextDisabled("  Ningún objeto usa este material.");
            for (SceneNode* u : users) ImGui::BulletText("%s", u->name().c_str());
        }
        ImGui::EndChild();
    }
    ImGui::End();
}

void drawAboutDialog() {
    if (!g.showAbout) return;
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::Begin("Acerca de Iron3D Studio", &g.showAbout, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse)) {
        if (g_fontBold) ImGui::PushFont(g_fontBold);
        ImGui::TextUnformatted("Iron3D Studio");
        if (g_fontBold) ImGui::PopFont();
        ImGui::TextUnformatted("Static Forge");
        ImGui::Separator();
        ImGui::TextDisabled("Interfaz inspirada en el flujo de trabajo de Cinema 4D R19.");
        ImGui::TextDisabled("Cinema 4D es una marca de Maxon; Iron3D no está afiliado a Maxon.");
        if (cinewareAvailable()) {
            ImGui::Separator();
            ImGui::TextDisabled("La importación de archivos .c4d usa el Cineware SDK");
            ImGui::TextDisabled("de MAXON Computer GmbH, integrado según su licencia.");
        }
    }
    ImGui::End();
}

void drawImportReport() {
    if (!g.showImport) return;
    const C4DImportReport& r = g.importReport;
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    const char* title = g.importError.empty() ? "Importar Cinema 4D###importrep" : "No se pudo abrir###importrep";
    if (ImGui::Begin(title, &g.showImport, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse)) {
        // Al abrir Iron3D con un .c4d el informe sale en el primer frame y las
        // ventanas que se crean despues lo taparian. Se queda delante de los
        // paneles mientras no haya un menu abierto ni otro dialogo con el foco.
        ImGuiWindow* self = ImGui::GetCurrentWindow();
        const ImGuiWindow* focused = ImGui::GetCurrentContext()->NavWindow;
        const bool otherDialog = focused && focused->RootWindow != self &&
                                 !(focused->RootWindow->Flags & ImGuiWindowFlags_NoBringToFrontOnFocus);
        if (!otherDialog && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId))
            ImGui::BringWindowToDisplayFront(self);
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 34.0f);
        if (g_fontBold) ImGui::PushFont(g_fontBold);
        ImGui::TextUnformatted(r.fileName.c_str());
        if (g_fontBold) ImGui::PopFont();
        if (!g.importError.empty()) {
            ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.40f, 1.0f), "%s", g.importError.c_str());
        } else {
            ImGui::Text("%d objetos, %d con malla, %d materiales, %d luces", r.objects, r.meshes, r.materials,
                        r.lights);
            ImGui::Text("%lld puntos, %lld polígonos", r.points, r.polygons);
            if (r.generatorsFromCache > 0)
                ImGui::Text("%d generadores con la geometría guardada por Cinema 4D", r.generatorsFromCache);
            if (r.primitivesRebuilt > 0)
                ImGui::Text("%d primitivas reconstruidas desde sus parámetros", r.primitivesRebuilt);
            ImGui::Separator();
            if (r.warnings.empty()) {
                ImGui::TextColored(ImVec4(0.45f, 0.85f, 0.45f, 1.0f), "Importado sin avisos.");
            } else {
                ImGui::TextUnformatted("Revisa:");
                for (const std::string& w : r.warnings) ImGui::BulletText("%s", w.c_str());
            }
        }
        ImGui::PopTextWrapPos();
        ImGui::Spacing();
        if (ImGui::Button("Cerrar", ImVec2(120, 0))) g.showImport = false;
    }
    ImGui::End();
}

void drawExportReport() {
    if (!g.showExport) return;
    const C4DExportReport& r = g.exportReport;
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::Begin("Exportado a Cinema 4D###exportrep", &g.showExport,
                     ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse)) {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 34.0f);
        if (g_fontBold) ImGui::PushFont(g_fontBold);
        ImGui::TextUnformatted(r.fileName.c_str());
        if (g_fontBold) ImGui::PopFont();
        ImGui::TextDisabled("%s", g.exportPath.c_str());
        ImGui::Text("%d objetos: %d mallas, %d primitivas, %d luces", r.objects, r.meshes, r.primitives, r.lights);
        ImGui::Text("%d materiales, %lld puntos, %lld polígonos", r.materials, r.points, r.polygons);
        ImGui::Separator();
        if (r.warnings.empty()) {
            ImGui::TextColored(ImVec4(0.45f, 0.85f, 0.45f, 1.0f), "Exportado sin avisos.");
        } else {
            ImGui::TextUnformatted("Revisa:");
            for (const std::string& w : r.warnings) ImGui::BulletText("%s", w.c_str());
        }
        ImGui::PopTextWrapPos();
        ImGui::Spacing();
        if (ImGui::Button("Cerrar", ImVec2(120, 0))) g.showExport = false;
    }
    ImGui::End();
}

} // namespace

void frameScene(OrbitalCamera& camera, const SceneNode& root) { frameNodes(camera, root, false); }
void frameNode(OrbitalCamera& camera, const SceneNode& node) { frameNodes(camera, node, true); }
void travelToNode(OrbitalCamera& camera, const SceneNode& node) { startCamTrip(camera, node); }

void showImportReport(const C4DImportReport& report) {
    g.importReport = report;
    g.importError.clear();
    g.showImport = true;
}

// Mayus+clic en el visor: anade o quita el objeto de la seleccion y devuelve el
// nuevo objeto activo (main.cpp hace el picking, la seleccion vive aqui).
SceneNode* toggleViewportSelection(SceneNode& root, SceneNode* hit, SceneNode* active) {
    if (!hit) return active;
    if (g.selIds.count(hit->id()) && g.selIds.size() > 1) {
        g.selIds.erase(hit->id());
        SceneNode* next = (hit == active) ? firstSelected(root) : active;
        g.lastSelectedId = next ? next->id() : -1;
        return next;
    }
    g.selIds.insert(hit->id());
    g.lastSelectedId = hit->id();
    return hit;
}

int selectionCount() { return (int)g.selIds.size(); }

// Centro de la seleccion en mundo: media de las posiciones de los objetos
// seleccionados (donde C4D planta el gizmo con varios activos).
glm::vec3 selectionPivot(SceneNode& root, SceneNode* active) {
    std::vector<SceneNode*> sel;
    collectSelected(root, sel);
    if (sel.empty()) return active ? glm::vec3(active->worldMatrix()[3]) : glm::vec3(0.0f);
    glm::vec3 sum(0.0f);
    for (SceneNode* n : sel) sum += glm::vec3(n->worldMatrix()[3]);
    return sum / (float)sel.size();
}

// Reparte un paso del arrastre del gizmo al resto de la seleccion: los demas
// copian la rotacion y la escala del objeto activo, y TODOS (el activo
// incluido) orbitan alrededor del pivote comun, como en C4D.
void applySelectionDrag(SceneNode& root, SceneNode* active, const glm::vec3& pivot,
                        const glm::vec3& worldDelta, const glm::vec3& rotDeltaDeg,
                        const glm::vec3& scaleRatio) {
    if (g.selIds.size() < 2) return;
    const bool moving = worldDelta != glm::vec3(0.0f);
    const bool rotating = rotDeltaDeg != glm::vec3(0.0f);
    const bool scaling = scaleRatio != glm::vec3(1.0f);
    if (!moving && !rotating && !scaling) return;
    // Misma composicion que SceneNode::localMatrix (HPB de C4D).
    const glm::mat3 rot = hpbToMat3(rotDeltaDeg);
    std::vector<SceneNode*> sel;
    collectSelected(root, sel);
    for (SceneNode* n : sel) {
        bool underSelected = false;
        for (SceneNode* p = n->parent(); p; p = p->parent())
            if (g.selIds.count(p->id())) { underSelected = true; break; }
        if (underSelected) continue;  // ya se mueve con su padre seleccionado
        if (n != active) {
            n->rotationDeg += rotDeltaDeg;
            n->scale *= scaleRatio;
        }
        const glm::vec3 world(n->worldMatrix()[3]);
        glm::vec3 target = world;
        if (rotating || scaling) target = pivot + rot * ((world - pivot) * scaleRatio);
        if (n != active && moving) target += worldDelta;
        if (target == world) continue;
        n->position = glm::vec3(glm::inverse(n->parentWorldMatrix()) * glm::vec4(target, 1.0f));
    }
}

// Arrastrar el gizmo mueve TODA la seleccion: el objeto activo lo mueve el
// gizmo y el resto recibe aqui el mismo desplazamiento en mundo (saltando los
// que cuelgan de otro seleccionado, que ya se mueven con su padre).
void applySelectionDelta(SceneNode& root, SceneNode* active, const glm::vec3& worldDelta) {
    if (g.selIds.size() < 2 || worldDelta == glm::vec3(0.0f)) return;
    std::vector<SceneNode*> sel;
    collectSelected(root, sel);
    for (SceneNode* n : sel) {
        if (n == active) continue;
        bool underSelected = false;
        for (SceneNode* p = n->parent(); p; p = p->parent())
            if (g.selIds.count(p->id())) { underSelected = true; break; }
        if (underSelected) continue;
        const glm::mat3 inv = glm::inverse(glm::mat3(n->parentWorldMatrix()));
        n->position += inv * worldDelta;
    }
}

// Fuentes del sistema (seccion 12 de la referencia), con tildes (Latin-1).
// Solo hace falta una vez -- a diferencia de los colores, el tema no cambia
// la tipografia, asi que esto no forma parte de applyTheme().
void loadC4DFonts() {
    ImGuiIO& io = ImGui::GetIO();
    const char* regular[] = {"C:/Windows/Fonts/segoeui.ttf", "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
                             "/usr/share/fonts/TTF/DejaVuSans.ttf"};
    const char* bold[] = {"C:/Windows/Fonts/segoeuib.ttf", "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
                          "/usr/share/fonts/TTF/DejaVuSans-Bold.ttf"};
    for (const char* f : regular) {
        std::error_code ec;
        if (std::filesystem::exists(f, ec) && io.Fonts->AddFontFromFileTTF(f, 15.0f)) break;
    }
    for (const char* f : bold) {
        std::error_code ec;
        if (std::filesystem::exists(f, ec)) {
            g_fontBold = io.Fonts->AddFontFromFileTTF(f, 15.0f);
            break;
        }
    }
    if (io.Fonts->Fonts.empty()) io.Fonts->AddFontDefault();
}

namespace {
ImU32 shadeU32(const glm::vec3& base, float delta) {
    const glm::vec3 c = glm::clamp(base + glm::vec3(delta), glm::vec3(0.0f), glm::vec3(1.0f)) * 255.0f;
    return IM_COL32((int)(c.x + 0.5f), (int)(c.y + 0.5f), (int)(c.z + 0.5f), 255);
}
} // namespace

// Aplica un preset de interfaz entero: recalcula ImGuiStyle::Colors[] y la
// paleta ImU32 de dibujado custom (kAmber/kStatusBg/etc.) a partir de los
// colores semilla, las metricas de estilo (redondeo, padding) y la
// disposicion/nombres de los paneles que usa drawMainUi. Se llama al arranque
// con el preset guardado en Preferencias y cada vez que Core cambia algo en
// Preferencias > Interfaz (vista previa en vivo).
void applyPreset(const UiPreset& preset) {
    g_preset = preset;
    g_names = &panelNames(preset.layout.arrangement);
    const UiTheme& theme = preset.theme;
    // Cromo: mismos grises que los literales de antes con el fondo C4D
    // (#3C3C3C -> 78, 52, 120, 48).
    kTabOn = shadeU32(theme.panelBg, 78.0f / 255.0f - 0.235f);
    kTabOff = shadeU32(theme.panelBg, 52.0f / 255.0f - 0.235f);
    kGrip = shadeU32(theme.panelBg, 120.0f / 255.0f - 0.235f);
    kIconGutter = shadeU32(theme.panelBg, 48.0f / 255.0f - 0.235f);

    static bool fontsLoaded = false;
    if (!fontsLoaded) {
        loadC4DFonts();
        fontsLoaded = true;
    }

    const DerivedPalette pal = derivePalette(theme);
    kAmber = pal.amberU32;
    kToolBlue = pal.toolBlueU32;
    kTabActive = pal.tabActiveU32;
    kSelOrange = pal.selOrangeU32;
    kHoverRow = pal.hoverRowU32;
    kTextCol = pal.textColU32;
    kTextDim = pal.textDimU32;
    kBorderDark = pal.borderDarkU32;
    kBorderLight = pal.borderLightU32;
    kSectionBg = pal.sectionBgU32;
    kStatusBg = pal.statusBgU32;
    kRulerBg = pal.rulerBgU32;
    kPlayhead = pal.playheadU32;

    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* c = style.Colors;

    const ImVec4& panelBg = pal.panelBg;
    const ImVec4& fieldBg = pal.fieldBg;
    const ImVec4& border = pal.border;
    const ImVec4& text = pal.text;
    const ImVec4& textDisabled = pal.textDisabled;
    const ImVec4& amber = pal.amber;
    const ImVec4& amberHover = pal.amberHover;
    const ImVec4& toolBlue = pal.toolBlue;
    const ImVec4& selection = pal.selection;

    const StyleMetrics& sm = preset.style;
    style.WindowRounding = sm.windowRounding;
    style.ChildRounding = 0.0f;
    style.FrameRounding = sm.frameRounding;
    style.PopupRounding = sm.popupRounding;
    style.GrabRounding = sm.grabRounding;
    style.TabRounding = sm.tabRounding;
    style.ScrollbarRounding = std::max(2.0f, sm.frameRounding);
    style.WindowBorderSize = 1.0f;
    style.ChildBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;
    style.PopupBorderSize = 1.0f;
    style.WindowPadding = ImVec2(6, 6);
    style.FramePadding = ImVec2(sm.framePadX, sm.framePadY);
    style.ItemSpacing = ImVec2(6, 4);
    style.ItemInnerSpacing = ImVec2(5, 4);
    style.ScrollbarSize = 12.0f;
    style.GrabMinSize = 8.0f;
    style.WindowMenuButtonPosition = ImGuiDir_None;
    style.SeparatorTextBorderSize = 1.0f;

    c[ImGuiCol_Text] = text;
    c[ImGuiCol_TextDisabled] = textDisabled;
    c[ImGuiCol_WindowBg] = panelBg;
    c[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_PopupBg] = pal.popupBg;
    c[ImGuiCol_Border] = border;
    c[ImGuiCol_FrameBg] = fieldBg;
    c[ImGuiCol_FrameBgHovered] = pal.frameBgHovered;
    c[ImGuiCol_FrameBgActive] = pal.frameBgActive;
    c[ImGuiCol_TitleBg] = pal.titleBg;
    c[ImGuiCol_TitleBgActive] = pal.titleBgActive;
    c[ImGuiCol_TitleBgCollapsed] = panelBg;
    c[ImGuiCol_MenuBarBg] = pal.menuBarBg;
    c[ImGuiCol_ScrollbarBg] = pal.scrollbarBg;
    c[ImGuiCol_ScrollbarGrab] = pal.scrollbarGrab;
    c[ImGuiCol_ScrollbarGrabHovered] = pal.scrollbarGrabHovered;
    c[ImGuiCol_ScrollbarGrabActive] = amber;
    c[ImGuiCol_Button] = pal.buttonCol;
    c[ImGuiCol_ButtonHovered] = pal.buttonHovered;
    c[ImGuiCol_ButtonActive] = toolBlue;
    c[ImGuiCol_Header] = selection;
    c[ImGuiCol_HeaderHovered] = pal.headerHovered;
    c[ImGuiCol_HeaderActive] = selection;
    c[ImGuiCol_Tab] = panelBg;
    c[ImGuiCol_TabHovered] = amberHover;
    c[ImGuiCol_TabSelected] = amber;
    c[ImGuiCol_TabDimmed] = panelBg;
    c[ImGuiCol_TabDimmedSelected] = pal.tabDimmedSelected;
    c[ImGuiCol_Separator] = border;
    c[ImGuiCol_SeparatorHovered] = amber;
    c[ImGuiCol_SeparatorActive] = amber;
    c[ImGuiCol_CheckMark] = amber;
    c[ImGuiCol_SliderGrab] = amber;
    c[ImGuiCol_SliderGrabActive] = amberHover;
    c[ImGuiCol_TableBorderLight] = border;
    c[ImGuiCol_TableBorderStrong] = border;
    c[ImGuiCol_DragDropTarget] = amber;
    c[ImGuiCol_NavHighlight] = amber;
    c[ImGuiCol_TextSelectedBg] = pal.textSelectedBg;
    c[ImGuiCol_ModalWindowDimBg] = ImVec4(0, 0, 0, 0.35f);
}


// --- Opciones de herramienta (rueda dentada de los menus) ------------------------------
void drawToolOptions() {
    if (g.optWin == UiState::OptWin::None) return;
    const char* title = g.optWin == UiState::OptWin::RectSel   ? "Selección Rectangular###toolopts"
                        : g.optWin == UiState::OptWin::LassoSel ? "Selección con Lazo###toolopts"
                                                                : "Conectar Objetos###toolopts";
    ImGui::SetNextWindowPos(g.optPos, ImGuiCond_Appearing);
    bool open = true;
    if (ImGui::Begin(title, &open, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse)) {
        if (g.optWin == UiState::OptWin::Connect) {
            ImGui::Checkbox("Soldar", &g.connectWeld);
            ImGui::BeginDisabled(!g.connectWeld);
            ImGui::SetNextItemWidth(120.0f);
            ImGui::InputFloat("Tolerancia (cm)", &g.connectWeldCm, 0.01f, 0.1f, "%.3f");
            g.connectWeldCm = std::clamp(g.connectWeldCm, 0.0f, 1000.0f);
            ImGui::EndDisabled();
            ImGui::TextDisabled("Une los puntos que coinciden (misma posición,\nnormal y color) al conectar.");
        } else {
            ImGui::Checkbox("Tolerante", &g.selTolerant);
            ImGui::TextDisabled(g.selTolerant ? "Se elige todo lo que toque la zona."
                                              : "Solo se elige lo que queda dentro entero.");
            ImGui::Checkbox("Solo Seleccionar Visibles", &g.selVisibleOnly);
            ImGui::TextDisabled(g.selVisibleOnly ? "Puntos, aristas y polígonos tapados por la malla no se eligen."
                                                 : "También se eligen los tapados (a través de la malla).");
        }
    }
    ImGui::End();
    if (!open) g.optWin = UiState::OptWin::None;
}

// --- Render en el Visor de Imagenes (Mayus+R) y Configuracion de Render (Ctrl+B) ------
void renderToPictureViewer(const SceneNode& root, const std::vector<Material>& materials, const OrbitalCamera& camera,
                           const ViewportState& vp) {
    auto& rs = g.render;
    int w = rs.width, h = rs.height;
    if (rs.useViewportSize) {
        w = std::max(16, (int)vp.w);
        h = std::max(16, (int)vp.h);
    }
    const auto t0 = std::chrono::steady_clock::now();
    const glm::mat4 proj = camera.projectionMatrix((float)w / (float)h, false);
    Thumbnail img = renderCameraImage(root, materials, camera.viewMatrix(), proj, w, h, rs.antialias,
                                      glm::vec3(rs.bgTop[0], rs.bgTop[1], rs.bgTop[2]),
                                      glm::vec3(rs.bgBottom[0], rs.bgBottom[1], rs.bgBottom[2]));
    auto& pv = g.pv;
    pv.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    pv.aa = rs.antialias;
    pv.viewName = viewLabel(camera.view(), "Perspectiva");
    freeUiTexture(pv.tex);
    pv.image = std::move(img);
    pv.tex = makeUiTexture(pv.image.rgba.data(), pv.image.width, pv.image.height);
    g.showPictureViewer = true;
    char buf[160];
    std::snprintf(buf, sizeof(buf), "Render: %d x %d en %.2f s", pv.image.width, pv.image.height, pv.seconds);
    flashStatus(buf);
}

void pngToStream(void* ctx, void* data, int size) { static_cast<std::ofstream*>(ctx)->write((const char*)data, size); }

bool savePng(const std::filesystem::path& file, const Thumbnail& img) {
    std::ofstream f(file, std::ios::binary);
    if (!f) return false;
    const int ok = stbi_write_png_to_func(pngToStream, &f, img.width, img.height, 4, img.rgba.data(), img.width * 4);
    return ok != 0 && f.good();
}

void drawPictureViewer(const SceneNode& root, const std::vector<Material>& materials, const OrbitalCamera& camera,
                       const ViewportState& vp) {
    if (!g.showPictureViewer) return;
    auto& pv = g.pv;
    ImGui::SetNextWindowSize(ImVec2(860, 640), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Visor de Imágenes", &g.showPictureViewer, ImGuiWindowFlags_MenuBar)) {
        ImGui::End();
        return;
    }
    if (ImGui::BeginMenuBar()) {
        if (ImGui::BeginMenu("Archivo")) {
            if (ImGui::MenuItem("Guardar como...", nullptr, false, !pv.image.rgba.empty())) {
                std::time_t now = std::time(nullptr);
                std::tm tmNow{};
#ifdef _WIN32
                localtime_s(&tmNow, &now);
#else
                localtime_r(&now, &tmNow);
#endif
                char name[64];
                std::strftime(name, sizeof(name), "render_%Y%m%d_%H%M%S.png", &tmNow);
                if (auto path = saveFileDialog("Guardar render", {{"Imagen PNG", "*.png"}}, name, "png")) {
                    flashStatus(savePng(*path, pv.image) ? "Render guardado: " + path->filename().string()
                                                         : std::string("No se pudo guardar el render."));
                }
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Ver")) {
            if (ImGui::MenuItem("Ajustar a la Ventana", nullptr, pv.zoom == 0.0f)) pv.zoom = 0.0f;
            if (ImGui::MenuItem("50 %", nullptr, pv.zoom == 0.5f)) pv.zoom = 0.5f;
            if (ImGui::MenuItem("100 %", nullptr, pv.zoom == 1.0f)) pv.zoom = 1.0f;
            if (ImGui::MenuItem("200 %", nullptr, pv.zoom == 2.0f)) pv.zoom = 2.0f;
            ImGui::EndMenu();
        }
        ImGui::EndMenuBar();
    }
    if (ImGui::Button("Renderizar")) renderToPictureViewer(root, materials, camera, vp);
    ImGui::SameLine();
    if (ImGui::Button("Configuración...")) g.showRenderSettings = true;
    ImGui::SameLine();
    if (pv.image.rgba.empty()) ImGui::TextDisabled("Todavía no hay ningún render (Mayús+R).");
    else
        ImGui::TextDisabled("%s · %d x %d · antialiasing %dx%d · %.2f s · %d triángulos", pv.viewName.c_str(),
                            pv.image.width, pv.image.height, pv.aa, pv.aa, pv.seconds, pv.image.triangles);
    ImGui::Separator();
    ImGui::BeginChild("##pvimg", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);
    if (pv.tex && pv.image.width > 0) {
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        float z = pv.zoom;
        if (z == 0.0f)
            z = std::min({1.0f, avail.x / (float)pv.image.width, avail.y / (float)pv.image.height});
        const ImVec2 sz((float)pv.image.width * z, (float)pv.image.height * z);
        const ImVec2 cur = ImGui::GetCursorPos();
        if (pv.zoom == 0.0f)
            ImGui::SetCursorPos(ImVec2(cur.x + std::max(0.0f, (avail.x - sz.x) * 0.5f),
                                       cur.y + std::max(0.0f, (avail.y - sz.y) * 0.5f)));
        ImGui::Image((ImTextureID)pv.tex, sz);
    }
    ImGui::EndChild();
    ImGui::End();
}

void drawRenderSettings() {
    if (!g.showRenderSettings) return;
    auto& rs = g.render;
    ImGui::SetNextWindowSize(ImVec2(440, 400), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Configuración de Render", &g.showRenderSettings)) {
        ImGui::End();
        return;
    }
    ImGui::SeparatorText("Salida");
    struct Preset { const char* name; int w, h; };
    static const Preset kPresets[] = {{"800 x 600", 800, 600},   {"640 x 480 (VGA)", 640, 480},
                                      {"1280 x 720 (HD 720)", 1280, 720}, {"1920 x 1080 (HD 1080)", 1920, 1080},
                                      {"3840 x 2160 (UHD 4K)", 3840, 2160}, {"1080 x 1080 (cuadrado)", 1080, 1080}};
    if (ImGui::BeginCombo("Preestablecido", "Elegir...")) {
        for (const Preset& p : kPresets)
            if (ImGui::Selectable(p.name)) { rs.width = p.w; rs.height = p.h; rs.useViewportSize = false; }
        ImGui::EndCombo();
    }
    ImGui::Checkbox("Usar el tamaño del visor", &rs.useViewportSize);
    ImGui::BeginDisabled(rs.useViewportSize);
    ImGui::InputInt("Anchura (px)", &rs.width, 10, 100);
    ImGui::InputInt("Altura (px)", &rs.height, 10, 100);
    rs.width = std::clamp(rs.width, 16, 8192);
    rs.height = std::clamp(rs.height, 16, 8192);
    ImGui::EndDisabled();
    ImGui::SeparatorText("Antialiasing");
    const char* aaNames[] = {"Ninguno (1x1)", "Geometría (2x2)", "3x3", "Mejor (4x4)"};
    int aaIdx = std::clamp(rs.antialias, 1, 4) - 1;
    if (ImGui::Combo("Antialiasing", &aaIdx, aaNames, 4)) rs.antialias = aaIdx + 1;
    ImGui::SeparatorText("Fondo");
    ImGui::ColorEdit3("Arriba", rs.bgTop);
    ImGui::ColorEdit3("Abajo", rs.bgBottom);
    ImGui::Spacing();
    ImGui::TextDisabled("Render por CPU con el sombreado de las miniaturas:\nsin sombras ni texturas todavía.");
    if (ImGui::Button("Renderizar en Visor de Imágenes")) g.renderRequested = true;
    ImGui::End();
}

// --- Modo Eje (L) -----------------------------------------------------------------------
void compensateAxisMove(SceneNode& node, const glm::mat4& beforeWorld) {
    const glm::mat4 after = node.worldMatrix();
    if (std::fabs(glm::determinant(glm::mat3(after))) < 1e-12f) return;
    const glm::mat4 t = glm::inverse(after) * beforeWorld;  // local nuevo <- local viejo
    bool same = true;
    for (int c = 0; c < 4 && same; ++c)
        for (int r = 0; r < 4; ++r)
            if (std::fabs(t[c][r] - (c == r ? 1.0f : 0.0f)) > 1e-6f) { same = false; break; }
    if (same) return;
    for (const auto& child : node.children()) child->setLocalMatrix(t * child->localMatrix());
    if (node.primitive == PrimitiveType::Mesh && node.mesh) {
        MeshData m = *node.mesh;
        const glm::mat3 nt = glm::transpose(glm::inverse(glm::mat3(t)));
        for (Vertex& v : m.vertices) {
            v.position = glm::vec3(t * glm::vec4(v.position, 1.0f));
            const glm::vec3 n = nt * v.normal;
            if (glm::length(n) > 1e-8f) v.normal = glm::normalize(n);
        }
        updateBounds(m);
        node.mesh = std::make_shared<const MeshData>(std::move(m));
    }
}

void notifyTransformEdited(SceneNode& root) {
    g.comp.dragStartMesh.reset();  // fin de un arrastre de componentes (si lo era)
    if (!g.autoKey) return;
    // Sin paso de deshacer propio: el del arrastre ya guardo el estado anterior.
    recordActiveObjects(root, nullptr, nullptr);
}

// --- Modos Puntos / Aristas / Poligonos -------------------------------------------------
const MeshData* editableMesh(const SceneNode* n) {
    return n && n->primitive == PrimitiveType::Mesh && n->mesh ? n->mesh.get() : nullptr;
}

// Topologia del objeto activo (se rehace si cambia de objeto o de malla) y
// limpieza de la seleccion si cambia el objeto, el modo o la conectividad.
bool syncComponents(const SceneNode* n) {
    auto& c = g.comp;
    const int mode = g_vp ? g_vp->compMode : 0;
    if (!mode || !editableMesh(n)) {
        c.sel.clear();
        c.nodeId = -1;
        return false;
    }
    if (c.nodeId != n->id() || c.mode != mode) c.sel.clear();
    c.nodeId = n->id();
    c.mode = mode;
    if (c.mesh == n->mesh) return true;
    const size_t oldPts = c.topo.points.size(), oldEdges = c.topo.edges.size(), oldPolys = c.topo.polys.size();
    c.mesh = n->mesh;
    c.topo = buildMeshTopology(*c.mesh);
    if (c.topo.points.size() != oldPts || c.topo.edges.size() != oldEdges || c.topo.polys.size() != oldPolys)
        c.sel.clear();  // otra conectividad: la seleccion ya no vale
    return true;
}

// Cuantos componentes hay en el modo actual.
size_t componentCount() {
    const auto& t = g.comp.topo;
    return g.comp.mode == 1 ? t.points.size() : g.comp.mode == 2 ? t.edges.size() : t.polys.size();
}

// Puntos afectados por la seleccion actual (segun el modo).
std::set<int> selectedPoints() {
    const auto& c = g.comp;
    const auto& t = c.topo;
    std::set<int> out;
    for (int i : c.sel) {
        if (c.mode == 1 && i < (int)t.points.size()) out.insert(i);
        else if (c.mode == 2 && i < (int)t.edges.size()) { out.insert(t.edges[(size_t)i].first); out.insert(t.edges[(size_t)i].second); }
        else if (c.mode == 3 && i < (int)t.polys.size()) for (int q : t.polys[(size_t)i]) out.insert(q);
    }
    return out;
}

struct CompProjector {
    glm::mat4 mvp;
    float x, y, w, h;
    bool operator()(const glm::vec3& local, ImVec2& out, float* depth = nullptr) const {
        const glm::vec4 c = mvp * glm::vec4(local, 1.0f);
        if (c.w <= 1e-5f) return false;
        out = ImVec2(x + (c.x / c.w * 0.5f + 0.5f) * w, y + (0.5f - c.y / c.w * 0.5f) * h);
        if (depth) *depth = c.z / c.w;
        return true;
    }
};

CompProjector compProjector(const SceneNode& n, const OrbitalCamera& camera, const ViewportState& vp) {
    return {camera.projectionMatrix(vp.w / std::max(1.0f, vp.h), false) * camera.viewMatrix() * n.worldMatrix(), vp.x, vp.y,
            vp.w, vp.h};
}

float segDist(ImVec2 p, ImVec2 a, ImVec2 b) {
    const float dx = b.x - a.x, dy = b.y - a.y, l2 = dx * dx + dy * dy;
    const float t = l2 > 1e-9f ? std::clamp(((p.x - a.x) * dx + (p.y - a.y) * dy) / l2, 0.0f, 1.0f) : 0.0f;
    const float qx = a.x + t * dx - p.x, qy = a.y + t * dy - p.y;
    return std::sqrt(qx * qx + qy * qy);
}

constexpr size_t kMaxCompDraw = 60000;  // por encima no se dibujan (rendimiento de ImGui)

// Que componentes del objeto activo se ven desde la camara (lo tapado por la
// propia malla no se dibuja ni se elige).
const ComponentVisibility& compVisibility(const SceneNode& n, const OrbitalCamera& camera, const ViewportState& vp) {
    auto& c = g.comp;
    const glm::mat4 mvp = compProjector(n, camera, vp).mvp;
    if (c.visMesh != c.mesh.get() || c.visMvp != mvp || c.visW != (int)vp.w || c.visH != (int)vp.h) {
        c.vis = computeComponentVisibility(*c.mesh, c.topo, mvp, (int)vp.w, (int)vp.h);
        c.visMesh = c.mesh.get();
        c.visMvp = mvp;
        c.visW = (int)vp.w;
        c.visH = (int)vp.h;
    }
    return c.vis;
}

void drawComponentOverlay(const SceneNode* selected, const OrbitalCamera& camera, const ViewportState& vp) {
    if (!syncComponents(selected)) return;
    const auto& c = g.comp;
    const auto& t = c.topo;
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    dl->PushClipRect(ImVec2(vp.x, vp.y), ImVec2(vp.x + vp.w, vp.y + vp.h), true);
    const CompProjector proj = compProjector(*selected, camera, vp);
    const ImU32 wire = IM_COL32(150, 170, 200, 150), sel = IM_COL32(255, 140, 30, 255), pt = IM_COL32(110, 160, 255, 255);
    if (t.edges.size() > kMaxCompDraw || t.points.size() > kMaxCompDraw) {
        dl->AddText(ImVec2(vp.x + 8, vp.y + 24), IM_COL32(255, 200, 120, 255), "Malla demasiado grande para dibujar sus componentes.");
        dl->PopClipRect();
        return;
    }
    const ComponentVisibility& vis = compVisibility(*selected, camera, vp);
    std::vector<ImVec2> sp(t.points.size());
    std::vector<char> ok(t.points.size());
    for (size_t i = 0; i < t.points.size(); ++i) ok[i] = proj(t.points[i], sp[i]);
    if (c.mode == 3) {
        const MeshData& m = *c.mesh;
        for (int i : c.sel) {
            if (i >= (int)t.polys.size() || !vis.polys[(size_t)i]) continue;
            for (int tri : t.polyTris[(size_t)i]) {
                ImVec2 q[3];
                bool inFront = true;
                for (int k = 0; k < 3; ++k)
                    inFront = inFront && proj(m.vertices[m.indices[3 * (size_t)tri + (size_t)k]].position, q[k]);
                if (inFront) dl->AddTriangleFilled(q[0], q[1], q[2], IM_COL32(255, 140, 30, 90));
            }
        }
    }
    std::set<std::pair<int, int>> selEdges;  // bordes de los poligonos seleccionados, resaltados
    if (c.mode == 3)
        for (int i : c.sel) {
            if (i >= (int)t.polys.size()) continue;
            const auto& pl = t.polys[(size_t)i];
            for (size_t k = 0; k < pl.size(); ++k) {
                const int a = pl[k], b = pl[(k + 1) % pl.size()];
                selEdges.insert({std::min(a, b), std::max(a, b)});
            }
        }
    for (size_t i = 0; i < t.edges.size(); ++i) {
        const auto [a, b] = t.edges[i];
        if (!ok[(size_t)a] || !ok[(size_t)b] || !vis.edges[i]) continue;
        const bool s = (c.mode == 2 && c.sel.count((int)i)) || selEdges.count(t.edges[i]);
        dl->AddLine(sp[(size_t)a], sp[(size_t)b], s ? sel : wire, s ? 2.0f : 1.0f);
    }
    if (c.mode == 1)
        for (size_t i = 0; i < t.points.size(); ++i) {
            if (!ok[i] || !vis.points[i]) continue;
            const bool s = c.sel.count((int)i) > 0;
            const float r = s ? 3.5f : 2.5f;
            dl->AddRectFilled(ImVec2(sp[i].x - r, sp[i].y - r), ImVec2(sp[i].x + r, sp[i].y + r), s ? sel : pt);
        }
    const char* names[] = {"", "Puntos", "Aristas", "Polígonos"};
    char buf[128];
    std::snprintf(buf, sizeof(buf), "%s: %zu de %zu seleccionados", names[c.mode], c.sel.size(), componentCount());
    dl->AddText(ImVec2(vp.x + 8, vp.y + 24), IM_COL32(230, 230, 230, 220), buf);
    dl->PopClipRect();
}

bool componentClick(SceneNode* selected, const OrbitalCamera& camera, float mx, float my, bool shift, bool ctrl) {
    if (!g_vp || !syncComponents(selected)) return false;
    auto& c = g.comp;
    const auto& t = c.topo;
    const CompProjector proj = compProjector(*selected, camera, *g_vp);
    const ComponentVisibility& vis = compVisibility(*selected, camera, *g_vp);
    const bool onlyVis = g.selVisibleOnly;
    const ImVec2 m(mx, my);
    int best = -1;
    float bestScore = 1e30f;
    if (c.mode == 1) {
        for (size_t i = 0; i < t.points.size(); ++i) {
            ImVec2 p;
            float z;
            if (onlyVis && !vis.points[i]) continue;
            if (!proj(t.points[i], p, &z)) continue;
            const float d = std::hypot(p.x - m.x, p.y - m.y);
            if (d < 9.0f && d + z < bestScore) { bestScore = d + z; best = (int)i; }
        }
    } else if (c.mode == 2) {
        for (size_t i = 0; i < t.edges.size(); ++i) {
            ImVec2 a, b;
            if (onlyVis && !vis.edges[i]) continue;
            if (!proj(t.points[(size_t)t.edges[i].first], a) || !proj(t.points[(size_t)t.edges[i].second], b)) continue;
            const float d = segDist(m, a, b);
            if (d < 6.0f && d < bestScore) { bestScore = d; best = (int)i; }
        }
    } else {
        for (size_t i = 0; i < t.polys.size(); ++i) {
            if (onlyVis && !vis.polys[i]) continue;
            std::vector<ImVec2> sp;
            bool inFront = true;
            for (int q : t.polys[i]) {
                ImVec2 p;
                inFront = inFront && proj(t.points[(size_t)q], p);
                sp.push_back(p);
            }
            if (!inFront || !pointInPolygon(sp, m)) continue;
            ImVec2 dummy;
            float depth = 0.0f;
            proj(polyCenter(t, (int)i), dummy, &depth);
            if (depth < bestScore) { bestScore = depth; best = (int)i; }
        }
    }
    if (ctrl) {
        if (best >= 0) c.sel.erase(best);
    } else {
        if (!shift) c.sel.clear();
        if (best >= 0) {
            if (shift && c.sel.count(best)) c.sel.erase(best);
            else c.sel.insert(best);
        }
    }
    return true;
}

// Seleccion Rectangular/Lazo en modo de componentes. true si la gestiona.
bool componentMarquee(SceneNode* selected, const OrbitalCamera& camera, const ViewportState& vp,
                      const std::function<bool(ImVec2)>& inside, bool shift, bool ctrl) {
    if (!syncComponents(selected)) return false;
    auto& c = g.comp;
    const auto& t = c.topo;
    const CompProjector proj = compProjector(*selected, camera, vp);
    const ComponentVisibility& vis = compVisibility(*selected, camera, vp);
    const bool onlyVis = g.selVisibleOnly;
    std::vector<char> in(t.points.size(), 0);
    for (size_t i = 0; i < t.points.size(); ++i) {
        ImVec2 p;
        in[i] = proj(t.points[i], p) && inside(p);
    }
    std::vector<int> hits;
    if (c.mode == 1) {
        for (size_t i = 0; i < t.points.size(); ++i)
            if (in[i] && (!onlyVis || vis.points[i])) hits.push_back((int)i);
    } else if (c.mode == 2) {
        for (size_t i = 0; i < t.edges.size(); ++i) {
            if (onlyVis && !vis.edges[i]) continue;
            const bool a = in[(size_t)t.edges[i].first], b = in[(size_t)t.edges[i].second];
            if (g.selTolerant ? (a || b) : (a && b)) hits.push_back((int)i);
        }
    } else {
        for (size_t i = 0; i < t.polys.size(); ++i) {
            if (onlyVis && !vis.polys[i]) continue;
            size_t n = 0;
            for (int q : t.polys[i]) n += in[(size_t)q] ? 1 : 0;
            if (g.selTolerant ? n > 0 : n == t.polys[i].size()) hits.push_back((int)i);
        }
    }
    if (ctrl) for (int h : hits) c.sel.erase(h);
    else {
        if (!shift) c.sel.clear();
        c.sel.insert(hits.begin(), hits.end());
    }
    return true;
}

// Borrar (Supr) en modo de componentes: quita los poligonos seleccionados o
// los que usan los puntos/aristas seleccionados (y los vertices sueltos).
bool deleteComponents(SceneNode& root, SceneNode* selected, SceneHistory& history) {
    if (!syncComponents(selected) || g.comp.sel.empty()) return false;
    auto& c = g.comp;
    const auto& t = c.topo;
    const std::set<int> pts = selectedPoints();
    std::set<std::pair<int, int>> selEdges;
    if (c.mode == 2)
        for (int i : c.sel) if (i < (int)t.edges.size()) selEdges.insert(t.edges[(size_t)i]);
    std::vector<char> keep(t.polys.size(), 1);
    size_t removed = 0;
    for (size_t p = 0; p < t.polys.size(); ++p) {
        const auto& pl = t.polys[p];
        bool drop = false;
        if (c.mode == 3) drop = c.sel.count((int)p) > 0;
        else if (c.mode == 1) for (int q : pl) drop = drop || pts.count(q) > 0;
        else
            for (size_t k = 0; k < pl.size() && !drop; ++k) {
                const int a = pl[k], b = pl[(k + 1) % pl.size()];
                drop = selEdges.count({std::min(a, b), std::max(a, b)}) > 0;
            }
        if (drop) { keep[p] = 0; ++removed; }
    }
    history.commitBefore(root);
    selected->mesh = std::make_shared<const MeshData>(keepPolygons(*selected->mesh, t, keep));
    c.sel.clear();
    flashStatus("Eliminados " + std::to_string(removed) + (removed == 1 ? " polígono." : " polígonos."));
    return true;
}

SceneNode* componentGizmoTarget(SceneNode* selected, bool dragging) {
    auto& c = g.comp;
    if (dragging && c.proxy && c.dragStartMesh) return c.proxy.get();
    if (!syncComponents(selected) || c.sel.empty()) return nullptr;
    const std::set<int> pts = selectedPoints();
    if (pts.empty()) return nullptr;
    glm::vec3 center(0.0f);
    for (int q : pts) center += c.topo.points[(size_t)q];
    center /= (float)pts.size();
    if (!c.proxy) c.proxy = std::make_unique<SceneNode>("componentes");
    const glm::mat4 w = selected->worldMatrix();
    glm::mat3 r(w);
    for (int i = 0; i < 3; ++i) r[i] = glm::length(r[i]) > 1e-8f ? glm::normalize(r[i]) : glm::vec3(i == 0, i == 1, i == 2);
    c.proxy->position = glm::vec3(w * glm::vec4(center, 1.0f));
    c.proxy->rotationDeg = mat3ToHpb(r);
    c.proxy->scale = glm::vec3(1.0f);
    return c.proxy.get();
}

void beginComponentDrag(SceneNode& node) {
    auto& c = g.comp;
    if (!c.proxy || !node.mesh) return;
    c.dragStartProxy = c.proxy->worldMatrix();
    c.dragStartMesh = node.mesh;
}

void applyComponentDrag(SceneNode& node) {
    auto& c = g.comp;
    if (!c.proxy || !c.dragStartMesh || c.dragStartMesh->vertices.size() != c.topo.vertexPoint.size()) return;
    const glm::mat4 w = node.worldMatrix();
    const glm::mat4 l = glm::inverse(w) * c.proxy->worldMatrix() * glm::inverse(c.dragStartProxy) * w;
    const std::set<int> pts = selectedPoints();
    MeshData m = *c.dragStartMesh;
    std::vector<char> moved(m.vertices.size(), 0);
    for (size_t v = 0; v < m.vertices.size(); ++v)
        if (pts.count(c.topo.vertexPoint[v])) {
            m.vertices[v].position = glm::vec3(l * glm::vec4(m.vertices[v].position, 1.0f));
            moved[v] = 1;
        }
    // Normales: se recalculan solo en los vertices de los triangulos tocados,
    // sumando las normales de cara (ponderadas por area) de cada vertice de
    // render -- las aristas vivas siguen vivas y las suaves, suaves.
    std::vector<char> touched(m.vertices.size(), 0);
    for (size_t i = 0; i + 2 < m.indices.size(); i += 3)
        if (moved[m.indices[i]] || moved[m.indices[i + 1]] || moved[m.indices[i + 2]])
            for (int k = 0; k < 3; ++k) touched[m.indices[i + (size_t)k]] = 1;
    std::vector<glm::vec3> acc(m.vertices.size(), glm::vec3(0.0f));
    for (size_t i = 0; i + 2 < m.indices.size(); i += 3) {
        const uint32_t a = m.indices[i], b = m.indices[i + 1], cc = m.indices[i + 2];
        if (!touched[a] && !touched[b] && !touched[cc]) continue;
        glm::vec3 n = glm::cross(m.vertices[b].position - m.vertices[a].position, m.vertices[cc].position - m.vertices[a].position);
        // Mismo giro que la normal que ya tenia (algunos triangulos vienen al reves).
        if (glm::dot(n, m.vertices[a].normal + m.vertices[b].normal + m.vertices[cc].normal) < 0.0f) n = -n;
        acc[a] += n;
        acc[b] += n;
        acc[cc] += n;
    }
    for (size_t v = 0; v < m.vertices.size(); ++v)
        if (touched[v] && glm::length(acc[v]) > 1e-12f) m.vertices[v].normal = glm::normalize(acc[v]);
    updateBounds(m);
    node.mesh = std::make_shared<const MeshData>(std::move(m));
    // La conectividad no cambia: se conserva la seleccion y solo se ponen al
    // dia las posiciones (dibujo y centro del gizmo).
    c.mesh = node.mesh;
    refreshTopologyPositions(c.topo, *node.mesh);
}

void releaseUiTextures() {
    freeUiTexture(g.pv.tex);
    g.pv.tex = nullptr;
}

void drawMainUi(SceneNode& root, SceneNode*& selected, GizmoMode& gizmoMode,
                 const std::filesystem::path& scenesDir, SceneHistory& history, float framerate,
                 RendererBackend activeBackend, Preferences& prefs, const std::filesystem::path& prefsPath,
                 std::vector<Material>& materials, OrbitalCamera& camera, ViewportState& viewport) {
    g_vp = &viewport;
    if (g.pendingWorldCoords) { viewport.worldCoords = true; g.pendingWorldCoords = false; }
    if (g.pendingAxisMode) { viewport.axisMode = true; g.pendingAxisMode = false; }
    g.hint.clear();

    // Seleccionar un objeto (en el visor o en el Gestor) devuelve el Gestor de
    // Atributos al objeto, como en C4D.
    const int selId = selected ? selected->id() : -1;
    if (selId != g.lastSelectedId) {
        if (selected && g.focus != AttrFocus::Tag) g.focus = AttrFocus::Object;
        if (!selected && g.focus == AttrFocus::Tag) g.focus = AttrFocus::Object;
        g.lastSelectedId = selId;
        // Si el cambio no viene de un Ctrl/Mayus+clic, la seleccion pasa a ser solo este.
        if (selId < 0) g.selIds.clear();
        else if (!g.selIds.count(selId)) { g.selIds.clear(); g.selIds.insert(selId); }
        viewport.renderView = false;  // cambiar la seleccion redibuja el visor (adios render)
    }

    // Reproduccion de la Linea de Tiempo (sin animacion todavia: solo avanza
    // el fotograma, con el FPS del proyecto).
    if (g.playDir != 0) {
        g.frame += ImGui::GetIO().DeltaTime * (float)g.fps * (float)g.playDir;
        const float lo = (float)g.previewMin, hi = (float)g.previewMax;
        if (g.frame > hi) g.frame = g.loop ? lo : hi;
        if (g.frame < lo) g.frame = g.loop ? hi : lo;
    }
    g.frame = std::clamp(g.frame, (float)g.minFrame, (float)g.maxFrame);
    g.previewMin = std::clamp(g.previewMin, g.minFrame, g.maxFrame);
    g.previewMax = std::clamp(g.previewMax, g.previewMin, g.maxFrame);
    // Animacion: al cambiar de fotograma la escena toma los valores de sus
    // pistas (lo que se haya movido sin grabar se pierde, como en C4D).
    if (g.frame != g.lastEvalFrame) {
        evaluateAnimation(root, g.frame);
        g.lastEvalFrame = g.frame;
    }
    if (g.recordRequested) {
        g.recordRequested = false;
        recordActiveObjects(root, selected, &history);
    }
    g.selKeys = selected ? keyFrames(*selected) : std::vector<int>{};

    handleShortcuts(root, selected, gizmoMode, history, materials, camera, viewport);
    runPendingCmd(root, selected, history, camera);
    // --ui-cmd=comp=...: despues de los demas comandos (p. ej. make-editable).
    if (g.pendingCompMode && g.cmdQueue.empty() && g.pendingCmd == UiCmd::None) {
        viewport.compMode = std::clamp(g.pendingCompMode, 0, 3);
        g.pendingCompMode = 0;
        if (syncComponents(selected)) g.comp.sel.insert(g.pendingCompSel.begin(), g.pendingCompSel.end());
    }
    if (g.compSelectAllRequested && !g.pendingCompMode && viewport.w > 64.0f && ImGui::GetFrameCount() > 5) {
        const bool keepVis = g.selVisibleOnly;
        g.selVisibleOnly = g.compSelectAllRequested == 1;
        componentMarquee(selected, camera, viewport, [](ImVec2) { return true; }, false, false);
        g.selVisibleOnly = keepVis;
        g.compSelectAllRequested = 0;
    }
    if (g.compDeleteRequested && !g.pendingCompMode) {
        g.compDeleteRequested = false;
        deleteComponents(root, selected, history);
    }
    {   // Ultima herramienta (Espacio): se apunta cualquier cambio, venga de donde venga.
        const int cur = viewport.selectTool ? 3 + viewport.selectShape : (int)gizmoMode;
        if (cur != g.curTool) {
            g.lastTool = g.curTool;
            g.curTool = cur;
        }
    }
    drawMainMenu(root, selected, gizmoMode, history, materials, camera, viewport);

    // Layout de paneles segun el preset de interfaz (UiPreset.cpp,
    // computeLayout), recalculado cada frame. Con el preset C4D salen los
    // mismos numeros de siempre (Entorno de Arranque, seccion 1).
    const ImGuiViewport* mv = ImGui::GetMainViewport();
    const float menuH = ImGui::GetFrameHeight();
    const float statusH = 22.0f;
    const float vpHeaderH = ImGui::GetFrameHeight() + 2.0f;
    const LayoutRects L = computeLayout(g_preset.layout, mv->Pos, mv->Size, menuH, statusH);
    const PanelNames& pn = names();

    drawTopToolbar(root, selected, gizmoMode, history, viewport, L.top.pos, L.top.size);
    drawLeftToolbar(L.left.pos, L.left.size);

    drawViewportHeader(root, selected, camera, viewport, L.viewport.pos, ImVec2(L.viewport.size.x, vpHeaderH));
    viewport.x = viewport.fullX = L.viewport.pos.x;
    viewport.y = viewport.fullY = L.viewport.pos.y + vpHeaderH;
    viewport.w = viewport.fullW = std::max(1.0f, L.viewport.size.x);
    viewport.h = viewport.fullH = std::max(1.0f, L.viewport.size.y - vpHeaderH);
    if (viewport.quad) drawQuadPanels(camera, viewport, root);
    drawViewportOverlay(camera, viewport, root);
    drawComponentOverlay(selected, camera, viewport);
    tickCamTrip(camera);
    drawOffscreenArrow(camera, viewport, selected);
    drawViewportContextMenu(root, selected, gizmoMode, history, viewport);
    handleMarqueeSelection(root, selected, camera, viewport);

    if (g.browserOpen) {
        ContentBrowserCallbacks cb;
        cb.load = [&](const PresetLibrary& lib, const PresetItem& item) {
            loadPresetImpl(lib, item, root, selected, history, materials);
        };
        cb.openLibrary = [] { openPresetLibraryDialog(); };
        cb.close = [] { g.browserOpen = false; };
        drawContentBrowser(L.objects.pos, L.objects.size, cb);
    } else {
        drawObjectManager(root, selected, history, materials, L.objects.pos, L.objects.size);
    }
    // Soltar en el visor un modelo arrastrado desde el navegador: se carga.
    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        const PresetLibrary* plib = nullptr;
        const PresetItem* pitem = nullptr;
        const ImVec2 m = ImGui::GetIO().MousePos;
        const bool overViewport = m.x >= viewport.x && m.y >= viewport.y && m.x < viewport.x + viewport.w &&
                                  m.y < viewport.y + viewport.h;
        if (overViewport && resolvePresetPayload(ImGui::GetDragDropPayload(), plib, pitem))
            loadPresetImpl(*plib, *pitem, root, selected, history, materials);
    }
    drawAttributesManager(root, selected, history, materials, L.attributes.pos, L.attributes.size);
    if (g_preset.layout.verticalTabs) {
        // La pestana "Navegador de Contenido" (solo en el preset C4D) cambia el
        // hueco del Gestor de Objetos; Tomas y Estructura aun no existen.
        int browserTab = -1;
        for (int i = 0; i < pn.topTabCount; ++i)
            if (pn.topTabs[i] && std::strcmp(pn.topTabs[i], "Navegador de Contenido") == 0) browserTab = i;
        const unsigned mask = 1u | (browserTab >= 0 ? (1u << browserTab) : 0u);
        if (L.objTabs.visible() && pn.topTabCount > 0) {
            const int clicked = drawVerticalTabs(L.objTabs.pos, L.objTabs.size, pn.topTabs, pn.topTabCount,
                                                 g.browserOpen && browserTab >= 0 ? browserTab : 0, mask);
            if (clicked >= 0) g.browserOpen = clicked == browserTab;
        }
        if (L.attrTabs.visible() && pn.sideTabCount > 0)
            drawVerticalTabs(L.attrTabs.pos, L.attrTabs.size, pn.sideTabs, pn.sideTabCount, 0);
    }

    drawTimeline(L.timeline.pos, L.timeline.size);
    drawMaterialManager(root, selected, history, materials, L.materials.pos, L.materials.size);
    drawCoordinatesManager(root, selected, history, L.coords.pos, L.coords.size);

    drawSceneDialog(root, selected, scenesDir, history, materials);
    drawPreferencesDialog(activeBackend, prefs, prefsPath);
    drawMaterialEditor(root, history, materials);
    drawAboutDialog();
    drawImportReport();
    drawExportReport();
    drawSavePresetDialog();
    drawCommander(root, selected, gizmoMode, history, materials, camera, viewport);
    if (g.renderRequested) {
        g.renderRequested = false;
        renderToPictureViewer(root, materials, camera, viewport);
    }
    drawRenderSettings();
    drawPictureViewer(root, materials, camera, viewport);
    drawToolOptions();

    drawStatusBar(framerate, gizmoMode, viewport, activeBackend, L.status.pos, L.status.size);
}

void drawIconSheet(ViewportState& viewport) {
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.w = 1.0f;
    viewport.h = 1.0f;
    const ImGuiViewport* mv = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(mv->Pos);
    ImGui::SetNextWindowSize(mv->Size);
    ImGui::Begin("##iconsheet", nullptr, kPanelFlags);
    const int total = (int)Icon::Count_;
    ImGui::Text("Iron3D Studio - hoja de iconos (%d)", total);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float cellW = 132.0f, cellH = 30.0f, iconS = 20.0f;
    const int cols = std::max(1, (int)(ImGui::GetContentRegionAvail().x / cellW));
    const ImVec2 o = ImGui::GetCursorScreenPos();
    for (int i = 0; i < total; ++i) {
        const ImVec2 p(o.x + (float)(i % cols) * cellW, o.y + (float)(i / cols) * cellH);
        dl->AddRectFilled(p, ImVec2(p.x + iconS + 6, p.y + iconS + 6), kIconGutter);
        icons::draw(dl, (Icon)i, ImVec2(p.x + 3, p.y + 3), iconS, true);
        dl->AddText(ImVec2(p.x + iconS + 10, p.y + 6), kTextCol, icons::name((Icon)i));
    }
    ImGui::Dummy(ImVec2((float)cols * cellW, (float)((total + cols - 1) / cols) * cellH));
    ImGui::End();
}

void openPreferencesDialog() { g.showPrefs = true; }

void openContentBrowser() { g.browserOpen = true; }

bool queueUiCommand(const std::string& cmd) {
    // Se encolan en orden (una por frame): --ui-cmd=select-all --ui-cmd=connect.
    if (cmd == "make-editable") g.cmdQueue.push_back(UiCmd::MakeEditable);
    else if (cmd == "frame-selected") g.cmdQueue.push_back(UiCmd::FrameSelected);
    else if (cmd == "select-all") g.cmdQueue.push_back(UiCmd::SelectAll);
    else if (cmd == "current-state") g.cmdQueue.push_back(UiCmd::CurrentState);
    else if (cmd == "connect") g.cmdQueue.push_back(UiCmd::Connect);
    else if (cmd == "connect-delete") g.cmdQueue.push_back(UiCmd::ConnectDelete);
    else if (cmd == "view-persp") g.cmdQueue.push_back(UiCmd::ViewPersp);
    else if (cmd == "view-top") g.cmdQueue.push_back(UiCmd::ViewTop);
    else if (cmd == "view-right") g.cmdQueue.push_back(UiCmd::ViewRight);
    else if (cmd == "view-front") g.cmdQueue.push_back(UiCmd::ViewFront);
    else if (cmd == "view-left") g.cmdQueue.push_back(UiCmd::ViewLeft);
    else if (cmd == "view-back") g.cmdQueue.push_back(UiCmd::ViewBack);
    else if (cmd == "view-bottom") g.cmdQueue.push_back(UiCmd::ViewBottom);
    else if (cmd == "view-quad") g.cmdQueue.push_back(UiCmd::ViewQuad);
    else if (cmd == "render-pv") g.renderRequested = true;
    else if (cmd == "key") g.recordRequested = true;
    else if (cmd == "comp-del") g.compDeleteRequested = true;
    else if (cmd == "sel-all-visible") g.compSelectAllRequested = 1;   // marco que cubre todo el visor
    else if (cmd == "sel-all-through") g.compSelectAllRequested = 2;
    else if (cmd.rfind("comp=", 0) == 0) {  // comp=<modo 1-3>[:indices separados por coma]
        try {
            const size_t colon = cmd.find(':');
            g.pendingCompMode = std::stoi(cmd.substr(5, colon == std::string::npos ? std::string::npos : colon - 5));
            g.pendingCompSel.clear();
            if (colon != std::string::npos) {
                std::stringstream ss(cmd.substr(colon + 1));
                std::string part;
                while (std::getline(ss, part, ',')) g.pendingCompSel.push_back(std::stoi(part));
            }
        } catch (...) { return false; }
    }
    else if (cmd.rfind("frame=", 0) == 0) { try { g.frame = (float)std::stoi(cmd.substr(6)); } catch (...) { return false; } }
    else if (cmd == "render-settings") g.showRenderSettings = true;
    else if (cmd == "opts-connect") g.optWin = UiState::OptWin::Connect, g.optPos = ImVec2(400, 200);
    else if (cmd == "opts-rect") g.optWin = UiState::OptWin::RectSel, g.optPos = ImVec2(400, 200);
    else if (cmd == "world-coords") { if (g_vp) g_vp->worldCoords = true; else g.pendingWorldCoords = true; }
    else if (cmd == "axis-mode") { if (g_vp) g_vp->axisMode = true; else g.pendingAxisMode = true; }
    else if (cmd == "vp-menu") g.openVpMenu = 30;
    else if (cmd.rfind("commander", 0) == 0) {
        g.showCommander = true;
        if (cmd.size() > 10 && cmd[9] == ':') g.commanderPreset = cmd.substr(10);
    } else return false;
    return true;
}

void loadPresetIntoScene(const PresetLibrary& lib, const PresetItem& item, SceneNode& root, SceneNode*& selected,
                         SceneHistory& history, std::vector<Material>& materials) {
    loadPresetImpl(lib, item, root, selected, history, materials);
}

} // namespace iron3d

namespace iron3d {
QuadRect quadPanelRect(const ViewportState& vp, int i) {
    constexpr float gap = 2.0f;
    const float hw = std::max(1.0f, (vp.fullW - gap) * 0.5f);
    const float hh = std::max(1.0f, (vp.fullH - gap) * 0.5f);
    const int col = i % 2, row = i / 2;
    return {vp.fullX + col * (hw + gap), vp.fullY + row * (hh + gap), hw, hh};
}
} // namespace iron3d
