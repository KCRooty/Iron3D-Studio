#include "MainUi.h"
#include "C4DIcons.h"
#include "UiPreset.h"
#include "UiTheme.h"
#include "core/I18n.h"
#include "core/SceneSerializer.h"
#include "core/MeshData.h"
#include "core/MeshTopology.h"
#include "core/MeshEdit.h"
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
#include "importers/obj/ObjIO.h"
#include "importers/smd/SmdIO.h"
#include "importers/gltf/GltfIO.h"
#include "core/IkSolver.h"
#include "core/Skin.h"
#include "core/Boolean.h"
#include "core/Clone.h"
#include "core/Deform.h"
#include "core/Symmetry.h"
#include "importers/lib4d/PresetTextures.h"
#include "core/ChannelBake.h"
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
namespace { void stepViewHistory(OrbitalCamera& camera, bool redo); }
const MeshData* editableMesh(const SceneNode* n);
// Modo de componentes con un objeto editable (1/2/3) o 0.
int componentModeFor(const SceneNode* n);
bool runCompOp(SceneNode& root, SceneNode* selected, SceneHistory& history, const std::string& op);

namespace {

using icons::Icon;

void drawKeyMenuItems();  // Claves Seleccionadas (Linea de Tiempo), mas abajo

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
                   CurrentState, Connect, ConnectDelete, BoolUnion, BoolSubtract, BoolIntersect, SelectAll, ViewPersp, ViewTop, ViewRight, ViewFront,
                   ViewLeft, ViewBack, ViewBottom, ViewQuad, ClearAnimation, SymmetryObject, Cut, Copy, Paste };

struct UiState {
    UiCmd pendingCmd = UiCmd::None;
    std::vector<UiCmd> cmdQueue;  // --ui-cmd (varias seguidas)
    // Seleccion Rectangular / Lazo en curso (puntos en coordenadas de ventana).
    bool marquee = false;
    std::vector<ImVec2> marqueePts;
    bool showCommander = false;  // Mayus+C
    std::string commanderPreset; // texto inicial (solo --ui-cmd=commander:texto)
    int renderIn = 0;
    std::string renderSave;  // --ui-cmd=render-save=...
    int renderSaveIn = 0;  // --ui-cmd=render: frames que faltan para renderizar
    int openVpMenu = 0;          // --ui-cmd=vp-menu: frames que faltan para abrirlo
    char commanderBuf[96] = {};
    int commanderSel = 0;
    std::string status;          // aviso corto en la barra de estado (p. ej. "2 objetos editables")
    double statusUntil = 0.0;
    // Ultima herramienta usada (Espacio alterna con ella, como en C4D).
    int lastTool = -1, curTool = -1;
    bool showScene = false;
    // Documento: ruta con la que se guardo/abrio (vacia = "Sin Nombre") y la escena tal como estaba al guardar;
    // hay cambios sin guardar si la escena actual ya no coincide con ella. Se toma en el primer fotograma y
    // otra vez tras Nuevo / Abrir (cleanInit = false).
    std::filesystem::path docPath;
    std::string cleanSnap;
    bool cleanInit = false;
    bool sceneLoadMode = false;
    bool showPrefs = false;
    bool showMatEditor = false;
    bool showAbout = false;
    bool showObjInfo = false;   // Información del Objeto
    std::set<std::string> dbgMenus;  // --ui-cmd=menu=A,B: menus que se abren solos (verificacion)
    int prefsPageReq = -1;           // --ui-cmd=prefs-page=N
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
    std::string importTitle = tr("Importar Cinema 4D"); // titulo del informe (c4d, obj...)
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
    // Buscar y Filtrar del Gestor de Objetos: texto (sin distinguir mayusculas) y tipos que se muestran.
    char omSearch[64] = {};
    bool omShowGeom = true, omShowLight = true, omShowCamera = true, omShowOther = true;
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
    // Portapapeles de objetos: copia propia (sobrevive a borrar el original).
    std::unique_ptr<SceneNode> clip;
    bool clipCut = false;  // viene de Cortar: el primer Pegar conserva los nombres

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
        bool studio = false;  // reflejos de estudio + transparencia (RenderFx)
        bool shadow = false;  // sombra suave en el suelo
        float bgTop[3] = {0.0f, 0.0f, 0.0f};    // C4D renderiza sobre negro
        float bgBottom[3] = {0.0f, 0.0f, 0.0f};
    } render;
    bool showRenderSettings = false;
    bool pendingWorldCoords = false, pendingAxisMode = false;  // --ui-cmd antes del primer frame
    int pendingCompMode = 0;
    std::vector<int> pendingCompSel;
    bool compDeleteRequested = false;  // --ui-cmd=comp-del
    bool symObjRequested = false;      // --ui-cmd=sym-obj: crear el objeto Simetria (tras comp=/comp-move=)
    bool compMoveRequested = false;    // --ui-cmd=comp-move=dx,dy,dz (cm, en el espacio del mundo): verificacion de la simetria
    glm::vec3 compMoveCm{0.0f};
    std::string compOp;                // Seleccion/Malla sobre componentes (menu o --ui-cmd=meshop=...)
    int compSelectAllRequested = 0;    // --ui-cmd=sel-all-visible|sel-all-through
    // Herramientas de modelado (Malla).
    enum class ModelTool { None, Extrude, InnerExtrude, Bevel, Knife };
    ModelTool modelTool = ModelTool::None;
    // Simetria de edicion (como la "True Symmetry" de R19): -1 = apagada, 0/1/2 = plano X/Y/Z local del objeto.
    // Al mover puntos, sus espejos sin seleccionar se mueven reflejados; los que estan sobre el plano no lo abandonan.
    int symAxis = -1;
    float symTolCm = 0.5f;
    float extrudeCm = 20.0f, innerInsetCm = 10.0f, bevelInsetCm = 10.0f, bevelExtrudeCm = 10.0f;
    bool knifeVisibleOnly = true;
    int knifeAxis = 1;       // modo Plano: eje local (0 X, 1 Y, 2 Z) que es la normal del plano
    bool knifePlane = false;  // Cuchillo en modo Plano
    bool knifeLoop = false;  // Cuchillo en modo Bucle (clic en una arista) en vez de Linea
    int chordM = -1000;  // fotograma en que se pulso M (atajos M~S)
    struct ToolDrag {
        bool active = false, applied = false;
        float startX = 0, startY = 0, curX = 0, curY = 0;
        float base = 0;                         // valor al empezar el arrastre
        float lastValue = 1e30f;
        std::shared_ptr<const MeshData> startMesh;
        MeshTopology startTopo;
        std::set<int> startSel;
    } tdrag;
    std::string pendingTool;  // --ui-cmd=tool=...|knife=...
    bool toolApplyRequested = false;  // boton Aplicar de la ventana de la herramienta

    // Opciones de herramienta (la rueda dentada de los menus, como en C4D).
    enum class OptWin { None, RectSel, LassoSel, Connect, Optimize };
    OptWin optWin = OptWin::None;
    ImVec2 optPos{0.0f, 0.0f};
    bool selTolerant = true;       // Seleccion Rectangular/Lazo: tocar basta
    bool selVisibleOnly = true;    // Solo Seleccionar Visibles (componentes tapados no cuentan)
    bool connectWeld = true;       // Conectar Objetos: soldar puntos coincidentes
    float optimizeTolCm = 0.01f;   // tolerancia de Optimizar, en cm
    float connectWeldCm = 0.01f;   // tolerancia de la soldadura, en cm

    // Animacion (F9): canales que se graban, claves automaticas y ultimo
    // fotograma evaluado (al cambiar de fotograma se recalcula la escena).
    bool keyPos = true, keyRot = true, keyScl = true;
    bool autoKey = false;
    bool recordRequested = false;
    float lastEvalFrame = -1e9f;
    std::vector<int> selKeys;  // claves del objeto activo (para la Linea de Tiempo)
    // Edicion de claves (Linea de Tiempo / Editor de Curvas): fotogramas
    // seleccionados y peticiones que se aplican en drawMainUi (alli hay
    // escena e historia).
    std::set<int> keySel;
    int keyDragDelta = 0;        // vista previa mientras se arrastran claves
    float keyDragStartX = 0.0f;
    enum class KeyReq { None, Move, Delete, Interp };
    KeyReq keyReq = KeyReq::None;
    int keyReqDelta = 0;
    KeyInterp keyReqInterp = KeyInterp::Spline;
    bool timelineHovered = false;
    bool showCurves = false;
    bool curvesHovered = false;
    bool curveChan[9] = {true, true, true, false, false, false, false, false, false};

    // Modos Puntos/Aristas/Poligonos: topologia del objeto activo (puntos =
    // vertices de render unidos por posicion; poligonos = triangulos),
    // seleccion de componentes y estado del arrastre.
    struct Components {
        std::shared_ptr<const MeshData> mesh;  // malla de la que se saco la topologia (se retiene: sin punteros reciclados)
        MeshTopology topo;
        // Visibilidad (z-buffer en CPU), en cache mientras no cambien malla, camara ni visor.
        ComponentVisibility vis;
        const MeshData* visMesh = nullptr;
        const void* visHidden = nullptr;
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
        flashStatus(tr("Grabar Objetos Activos: no hay ningún objeto seleccionado."));
        return 0;
    }
    if (!g.keyPos && !g.keyRot && !g.keyScl) {
        flashStatus(tr("Grabar Objetos Activos: Posición, Escala y Rotación están desactivadas."));
        return 0;
    }
    if (history) history->commitBefore(root);
    const int f = (int)std::lround(g.frame);
    for (SceneNode* n : nodes) recordKey(*n, f, g.keyPos, g.keyRot, g.keyScl);
    g.lastEvalFrame = g.frame;  // los valores actuales ya son los de este fotograma
    flashStatus(tr("Clave grabada en el fotograma ") + std::to_string(f) + " (" + std::to_string(nodes.size()) +
                (nodes.size() == 1 ? tr(" objeto).") : tr(" objetos).")));
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
const char* modelToolName(UiState::ModelTool t) {
    switch (t) {
        case UiState::ModelTool::Extrude: return tr("Extrusión");
        case UiState::ModelTool::InnerExtrude: return tr("Extrusión Interna");
        case UiState::ModelTool::Bevel: return tr("Bisel");
        case UiState::ModelTool::Knife: return tr("Cuchillo");
        default: return "";
    }
}

void setModelTool(UiState::ModelTool t) {
    g.modelTool = t;
    g.tdrag = {};
    if (t == UiState::ModelTool::None) return;
    if (g_vp) g_vp->selectTool = false;
    // Extruir y Biselar trabajan con poligonos: se pasa a ese modo (como C4D).
    if (t != UiState::ModelTool::Knife && g_vp) g_vp->compMode = 3;
    flashStatus(std::string(modelToolName(t)) + (t == UiState::ModelTool::Knife
                                                    ? tr(": arrastra en el visor para dibujar la línea de corte.")
                                                    : tr(": arrastra en horizontal en el visor (o usa Aplicar).")));
}

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
        case CamView::Top: return tr("Superior");
        case CamView::Right: return tr("Derecha");
        case CamView::Front: return tr("Frontal");
        case CamView::Left: return tr("Izquierda");
        case CamView::Back: return tr("Trasera");
        case CamView::Bottom: return tr("Inferior");
        default: return perspName;
    }
}

// --- Utilidades -----------------------------------------------------------------

const char* defaultNameFor(PrimitiveType type) {
    switch (type) {
        case PrimitiveType::Cube: return tr("Cubo");
        case PrimitiveType::Sphere: return tr("Esfera");
        case PrimitiveType::Plane: return tr("Plano");
        case PrimitiveType::Light: return tr("Luz");
        case PrimitiveType::Camera: return tr("Cámara");
        case PrimitiveType::Mesh: return tr("Polígono");
        case PrimitiveType::None: default: return tr("Nulo");
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
    for (MaterialPart& part : node.materialParts) {
        if (part.materialIndex == removed) part.materialIndex = -1;
        else if (part.materialIndex > removed) part.materialIndex -= 1;
    }
    for (const auto& child : node.children()) fixupMaterialIndicesAfterRemoval(*child, removed);
}

bool materialUsed(const SceneNode& node, int index) {
    if (node.materialIndex == index) return true;
    for (const MaterialPart& part : node.materialParts)
        if (part.materialIndex == index) return true;
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
// `onlyVisible`: solo lo que se ve en el editor (2026-11-10). Los objetivos
// ocultos de un Pose Morph de un .lib4d (a cientos de unidades, en la pose
// del bind) dejaban el personaje como un punto en el centro del visor.
void accumulateBounds(const SceneNode& node, glm::vec3& mn, glm::vec3& mx, bool& any, bool onlyVisible = false,
                      bool parentVisible = true) {
    const bool vis = resolveVisibility(node.editorVis, parentVisible);
    if (onlyVisible && !vis && node.children().empty()) return;
    if (node.primitive != PrimitiveType::None && (!onlyVisible || vis)) {
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
    for (const auto& c : node.children()) accumulateBounds(*c, mn, mx, any, onlyVisible, vis);
}

bool nodeEditorVisible(const SceneNode* n);

// Encuadra lo visible; si no hay nada visible (p. ej. encuadrar un objeto
// oculto seleccionado), todo.
void frameNodes(OrbitalCamera& camera, const SceneNode& node, bool includeSelf) {
    glm::vec3 mn(0.0f), mx(0.0f);
    bool any = false;
    const bool parentVis = node.parent() ? nodeEditorVisible(node.parent()) : true;
    for (bool onlyVisible : {true, false}) {
        if (includeSelf) accumulateBounds(node, mn, mx, any, onlyVisible, parentVis);
        else
            for (const auto& c : node.children()) accumulateBounds(*c, mn, mx, any, onlyVisible, nodeEditorVisible(&node));
        if (any) break;
    }
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
    setHint(tr("Parámetro todavía no implementado en Iron3D"));
}

void roCombo(const char* label, const char* value, bool animatable = false) {
    propLabel(label, animatable);
    ImGui::BeginDisabled();
    ImGui::SetNextItemWidth(fieldWidth());
    ImGui::PushID(label);
    if (ImGui::BeginCombo("##v", value)) ImGui::EndCombo();
    ImGui::PopID();
    ImGui::EndDisabled();
    setHint(tr("Parámetro todavía no implementado en Iron3D"));
}

void roCheck(const char* label, bool value, bool animatable = true) {
    propLabel(label, animatable);
    ImGui::BeginDisabled();
    ImGui::PushID(label);
    ImGui::Checkbox("##v", &value);
    ImGui::PopID();
    ImGui::EndDisabled();
    setHint(tr("Parámetro todavía no implementado en Iron3D"));
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
        if (hov) g.hint = tr("Clic: mostrar esta página. Mayús+Clic: mostrar varias a la vez.");
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

std::vector<SceneNode*> topLevelSelection(SceneNode& root, SceneNode* clicked);
void selectOnly(SceneNode*& selected, SceneNode* n);

// Hace que createObject cree siempre como hijo del seleccionado (menu "Anadir Hijo" del Gestor de Objetos).
bool g_forceChild = false;

// Seccion 18.6 (combinaciones de C4D al crear un objeto con el seleccionado):
//   sin tecla -> raiz de la escena      Alt -> hijo del seleccionado
//   Ctrl -> hermano DEBAJO              Ctrl+Mayus -> hermano ARRIBA
//   Alt+Ctrl -> padre de toda la seleccion (la seleccion pasa a ser hijo del objeto nuevo)
// (Alt+Mayus = un hijo en cada seleccionado se resuelve en createPaletteItem.) Siempre queda seleccionado.
SceneNode* createObject(SceneNode& root, SceneNode*& selected, SceneHistory& history, PrimitiveType type) {
    const ImGuiIO& io = ImGui::GetIO();
    const bool alt = io.KeyAlt || g_forceChild;
    SceneNode* parent = &root;
    size_t at = root.children().size();
    std::vector<SceneNode*> wrapped;
    if (selected && selected->parent()) {
        if (alt && io.KeyCtrl && !g_forceChild) {
            wrapped = topLevelSelection(root, selected);
            parent = wrapped.front()->parent();
            at = parent->indexOf(wrapped.front());
        } else if (alt) {
            parent = selected;
            at = selected->children().size();
        } else if (io.KeyCtrl) {
            parent = selected->parent();
            at = parent->indexOf(selected) + (io.KeyShift ? 0 : 1);
        }
    }
    history.commitBefore(root);
    SceneNode* made = parent->insertChild(std::make_unique<SceneNode>(defaultNameFor(type), type), at);
    if (!wrapped.empty()) {
        SceneNode::reparentNodes(wrapped, *made, made->children().size());
        selectOnly(selected, made);
    }
    selected = made;
    g.focus = AttrFocus::Object;
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
    g.docPath.clear();
    g.cleanInit = false;
}

void offsetMaterialIndices(SceneNode& node, int base) {
    if (node.materialIndex >= 0) node.materialIndex += base;
    for (MaterialPart& part : node.materialParts)
        if (part.materialIndex >= 0) part.materialIndex += base;
    for (const auto& c : node.children()) offsetMaterialIndices(*c, base);
}

void reportFileError(const std::filesystem::path& path, const std::string& err) {
    g.importReport = C4DImportReport{};
    g.importTitle = tr("Importar Cinema 4D");
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
        reportFileError(tr("Abrir librería"), tr("Abrir una librería necesita el diálogo de archivos del sistema, que en "
                                          "esta plataforma todavía no existe. Cópiala a library/browser."));
        return;
    }
    const auto path = openFileDialog(tr("Abrir librería de preestablecidos"),
                                     {{tr("Librerías de preestablecidos de Cinema 4D"), "*.lib4d"}});
    if (path) openPresetLibrary(*path);
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
    if (data.size() < 8 || std::memcmp(data.data(), tr("QC4D"), 4) != 0)
        return reportNamedError(item.name, tr("Este preestablecido no es un modelo de Cinema 4D."));
    std::error_code ec;
    const auto dir = std::filesystem::temp_directory_path(ec) / tr("Iron3D");
    std::filesystem::create_directories(dir, ec);
    const auto tmp = dir / ("preset_" + std::to_string(item.id) + ".c4d");
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        f.write(reinterpret_cast<const char*>(data.data()), std::streamsize(data.size()));
        if (!f) return reportNamedError(item.name, tr("No se pudo escribir el archivo temporal en ") + pathUtf8(dir) + ".");
    }
    data.clear();
    data.shrink_to_fit();
    const std::string before = history.snapshot(root);
    C4DImportReport rep;
    const size_t firstNewMaterial = materials.size();
    const bool ok = importC4D(tmp, root, materials, rep, &err);
    std::filesystem::remove(tmp, ec);
    if (!ok) return reportNamedError(item.name, err);
    // Texturas: de esta libreria o de cualquier otra montada, cada una en su carpeta.
    const PresetTextureReport texRep =
        extractPresetTextures(lib, mountedLibraries(), dir / "texcache", materials, firstNewMaterial);
    for (const std::string& t : texRep.missing)
        rep.warnings.push_back(tr("Textura que no está en ninguna librería montada: ") + t);
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

// Importa un OBJ en `dest` y deja el informe listo para mostrar.
bool importObjInto(const std::filesystem::path& path, SceneNode& dest, std::vector<Material>& mats) {
    ObjReport orep;
    std::string err;
    if (!importObj(path, dest, mats, orep, &err)) {
        reportFileError(path, err);
        return false;
    }
    g.importReport = C4DImportReport{};
    g.importReport.fileName = pathUtf8(path.filename());
    g.importReport.objects = orep.objects;
    g.importReport.meshes = orep.objects;
    g.importReport.points = orep.points;
    g.importReport.polygons = orep.polygons;
    g.importReport.materials = orep.materials;
    g.importReport.warnings = orep.warnings;
    g.importError.clear();
    g.importTitle = tr("Importar OBJ");
    g.showImport = true;
    return true;
}

bool importGltfInto(const std::filesystem::path& path, SceneNode& dest, std::vector<Material>& mats) {
    GltfReport grep;
    std::string err;
    if (!importGltf(path, dest, mats, grep, &err)) {
        reportFileError(path, err);
        return false;
    }
    g.importReport = C4DImportReport{};
    g.importReport.fileName = pathUtf8(path.filename());
    g.importReport.objects = grep.nodes;
    g.importReport.meshes = grep.meshes;
    g.importReport.points = grep.points;
    g.importReport.polygons = grep.polygons;
    g.importReport.materials = grep.materials;
    g.importReport.warnings = grep.warnings;
    g.importError.clear();
    g.importTitle = tr("Importar glTF");
    g.showImport = true;
    return true;
}

// Archivo > Abrir (Ctrl+O): .iron o .c4d con el dialogo del sistema. Sin
// dialogo nativo (Linux por ahora) se usa el propio de la carpeta scenes/.
// Guardar (Ctrl+S) / Guardar como (Ctrl+Mayus+S) con el dialogo del sistema, como C4D. Sin ruta todavia
// (Sin Nombre) Guardar pide una. true = quedo guardado.
bool saveDocument(SceneNode& root, SceneHistory& history, std::vector<Material>& materials, bool forceDialog) {
    std::filesystem::path path = g.docPath;
    if (forceDialog || path.empty()) {
        if (!nativeFileDialogAvailable()) {
            g.showScene = true;
            g.sceneLoadMode = false;
            return false;
        }
        const auto p = saveFileDialog(tr("Guardar Archivo"), {{"Iron3D", "*.iron"}},
                                      path.empty() ? std::string("Sin Nombre 1.iron") : path.filename().string(), "iron");
        if (!p) return false;
        path = *p;
    }
    std::string err;
    if (!saveScene(path, root, materials, &err)) {
        reportFileError(path, err);
        return false;
    }
    g.docPath = path;
    g.cleanSnap = history.snapshot(root);
    g.cleanInit = true;
    return true;
}

// Guardar incrementalmente: guarda con el siguiente numero libre junto al archivo actual (nombre_001.iron...) y
// el documento pasa a ser ese. Sin ruta todavia no hace nada. true = guardado.
bool saveIncremental(SceneNode& root, SceneHistory& history, std::vector<Material>& materials) {
    if (g.docPath.empty()) return false;
    std::string stem = g.docPath.stem().string();
    // Si ya termina en _NNN se sigue la serie en vez de apilar sufijos.
    const size_t us = stem.rfind('_');
    if (us != std::string::npos && us + 1 < stem.size() && stem.find_first_not_of("0123456789", us + 1) == std::string::npos)
        stem.resize(us);
    std::filesystem::path path;
    for (int i = 1; i < 10000; ++i) {
        char suffix[16];
        std::snprintf(suffix, sizeof(suffix), "_%03d", i);
        path = g.docPath.parent_path() / (stem + suffix + ".iron");
        std::error_code ec;
        if (!std::filesystem::exists(path, ec)) break;
    }
    std::string err;
    if (!saveScene(path, root, materials, &err)) {
        reportFileError(path, err);
        return false;
    }
    g.docPath = path;
    g.cleanSnap = history.snapshot(root);
    g.cleanInit = true;
    flashStatus(std::string(tr("Guardado como ")) + path.filename().string());
    return true;
}

bool hasUnsavedChanges(const SceneNode& root, const SceneHistory& history) {
    return g.cleanInit && history.snapshot(root) != g.cleanSnap;
}

// Si hay cambios sin guardar pregunta Si / No / Cancelar. true = se puede seguir (guardado o descartado).
bool confirmDiscardChanges(SceneNode& root, SceneHistory& history, std::vector<Material>& materials, const char* beforeWhat) {
    if (!hasUnsavedChanges(root, history)) return true;
    const std::string name = g.docPath.empty() ? std::string("Sin Nombre 1") : g.docPath.stem().string();
    char text[512];
    std::snprintf(text, sizeof(text), tr("¿Quiere guardar los cambios en el proyecto \"%s\" antes de %s?"), name.c_str(), beforeWhat);
    switch (askSaveChanges("Iron3D Studio", text)) {
        case SaveChoice::Yes: return saveDocument(root, history, materials, false);
        case SaveChoice::No: return true;
        default: return false;
    }
}

bool confirmQuitImpl(SceneNode& root, SceneHistory& history, std::vector<Material>& materials) {
    return confirmDiscardChanges(root, history, materials, tr("salir"));
}

void openSceneFile(SceneNode& root, SceneNode*& selected, SceneHistory& history, std::vector<Material>& materials,
                   OrbitalCamera& camera) {
    if (!nativeFileDialogAvailable()) {
        g.showScene = true;
        g.sceneLoadMode = true;
        return;
    }
    const auto path = openFileDialog(tr("Abrir"), {{tr("Escenas y librerías (iron, c4d, obj, glb, lib4d)"), "*.iron;*.c4d;*.obj;*.glb;*.gltf;*.lib4d"},
                                               {"Iron3D", "*.iron"}, {tr("Cinema 4D"), "*.c4d"}, {tr("Wavefront"), "*.obj"},
                                               {"glTF", "*.glb;*.gltf"},
                                               {tr("Librería de preestablecidos de Cinema 4D"), "*.lib4d"}});
    if (!path) return;
    // Una .lib4d no sustituye la escena: se monta en el Navegador de Contenido
    // (Preestablecidos, solo esta sesion) y se abre su ventana, como en C4D.
    if (isLib4dPath(*path)) return openPresetLibrary(*path);
    std::string err;
    if (isObjPath(*path) || isGltfPath(*path)) {
        SceneNode temp("tmp");
        std::vector<Material> mats;
        if (isGltfPath(*path) ? !importGltfInto(*path, temp, mats) : !importObjInto(*path, temp, mats)) return;
        root.clearChildren();
        root.adoptChildrenFrom(temp);
        materials = std::move(mats);
    } else if (isC4DPath(*path)) {
        SceneNode temp("tmp");
        std::vector<Material> mats;
        C4DImportReport rep;
        if (!importC4D(*path, temp, mats, rep, &err)) return reportFileError(*path, err);
        root.clearChildren();
        root.adoptChildrenFrom(temp);
        materials = std::move(mats);
        g.importReport = rep;
        g.importError.clear();
        g.importTitle = tr("Importar Cinema 4D");
        g.showImport = true;
    } else if (!loadScene(*path, root, materials, &err)) {
        return reportFileError(*path, err);
    }
    selected = nullptr;
    history.reset();
    g.selectedMat = -1;
    g.docPath = isC4DPath(*path) || isObjPath(*path) || isGltfPath(*path) ? std::filesystem::path() : *path;
    g.cleanInit = false;
    frameNodes(camera, root, false);
}

// Archivo > Fusionar (Ctrl+Mayus+O): anade a la escena actual lo que haya en
// otro .c4d o .iron, como en C4D. Se puede deshacer.
void mergeSceneFile(SceneNode& root, SceneNode*& selected, SceneHistory& history, std::vector<Material>& materials) {
    if (!nativeFileDialogAvailable()) {
        reportFileError(tr("Fusionar"), tr("Fusionar necesita el diálogo de archivos del sistema, que en esta "
                                    "plataforma todavía no existe."));
        return;
    }
    const auto path = openFileDialog(tr("Fusionar"), {{tr("Escenas de Cinema 4D, OBJ, glTF e Iron3D"), "*.c4d;*.obj;*.glb;*.gltf;*.iron"},
                                                  {tr("Cinema 4D"), "*.c4d"}, {tr("Wavefront"), "*.obj"},
                                                  {"glTF", "*.glb;*.gltf"}, {tr("Iron3D"), "*.iron"}});
    if (!path) return;
    const std::string before = history.snapshot(root);
    std::string err;
    if (isObjPath(*path)) {
        if (!importObjInto(*path, root, materials)) return;
    } else if (isGltfPath(*path)) {
        if (!importGltfInto(*path, root, materials)) return;
    } else if (isC4DPath(*path)) {
        C4DImportReport rep;
        if (!importC4D(*path, root, materials, rep, &err)) return reportFileError(*path, err);
        g.importReport = rep;
        g.importError.clear();
        g.importTitle = tr("Importar Cinema 4D");
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
        reportNamedError(title, tr("Exportar necesita el diálogo de archivos del sistema, que en esta plataforma "
                                "todavía no existe."));
        return;
    }
    const auto path = saveFileDialog(title, {{tr("Cinema 4D"), "*.c4d"}}, defaultName, "c4d");
    if (!path) return;
    std::string err;
    C4DExportReport rep;
    if (!exportC4D(*path, roots, materials, allMaterials, rep, &err, g.fps)) return reportFileError(*path, err);
    g.exportReport = rep;
    g.exportPath = pathUtf8(*path);
    g.showExport = true;
}

// Archivo > Exportar > Valve SMD (*.smd): toda la escena como malla de referencia de Source.
void exportSceneSmd(SceneNode& root, const std::vector<Material>& materials) {
    std::vector<const SceneNode*> roots;
    for (const auto& c : root.children()) roots.push_back(c.get());
    if (roots.empty()) return reportNamedError(tr("Exportar"), tr("La escena está vacía."));
    if (!nativeFileDialogAvailable())
        return reportNamedError(tr("Exportar"), tr("Exportar necesita el diálogo de archivos del sistema, que en esta plataforma todavía no existe."));
    const auto path = saveFileDialog(tr("Exportar a Source (SMD)"), {{tr("Valve SMD"), "*.smd"}}, tr("Escena.smd"), "smd");
    if (!path) return;
    SmdReport srep;
    std::string err;
    if (!exportSmd(*path, roots, materials, srep, &err)) return reportFileError(*path, err);
    g.importReport = C4DImportReport{};
    g.importReport.fileName = pathUtf8(path->filename());
    g.importReport.objects = srep.objects;
    g.importReport.meshes = srep.objects;
    g.importReport.polygons = srep.triangles;
    g.importReport.materials = (int)srep.materials.size();
    g.importReport.warnings = srep.warnings;
    if (srep.bones > 1) {
        char msg[256];
        std::snprintf(msg, sizeof(msg), tr("SMD: %d huesos y %d mallas con pesos; solo la pose actual, sin animación. Cada material de Source necesita su .vmt."), srep.bones, srep.skinnedMeshes);
        g.importReport.warnings.push_back(msg);
    } else {
        g.importReport.warnings.push_back(tr("SMD: solo geometría, normales y UV; sin esqueleto ni animación. Cada material de Source necesita su .vmt."));
    }
    g.importError.clear();
    g.importTitle = tr("Exportar SMD");
    g.showImport = true;
}

// Archivo > Exportar > Wavefront (*.obj): toda la escena.
void exportSceneObj(SceneNode& root, const std::vector<Material>& materials) {
    std::vector<const SceneNode*> roots;
    for (const auto& c : root.children()) roots.push_back(c.get());
    if (roots.empty()) return reportNamedError(tr("Exportar"), tr("La escena está vacía."));
    if (!nativeFileDialogAvailable())
        return reportNamedError(tr("Exportar"), tr("Exportar necesita el diálogo de archivos del sistema, que en esta plataforma todavía no existe."));
    const auto path = saveFileDialog(tr("Exportar a Wavefront"), {{tr("Wavefront"), "*.obj"}}, tr("Escena.obj"), "obj");
    if (!path) return;
    ObjReport orep;
    std::string err;
    if (!exportObj(*path, roots, materials, orep, &err)) return reportFileError(*path, err);
    g.importReport = C4DImportReport{};
    g.importReport.fileName = pathUtf8(path->filename());
    g.importReport.objects = orep.objects;
    g.importReport.meshes = orep.objects;
    g.importReport.points = orep.points;
    g.importReport.polygons = orep.polygons;
    g.importReport.materials = orep.materials;
    g.importReport.warnings = orep.warnings;
    g.importError.clear();
    g.importTitle = tr("Exportar OBJ");
    g.showImport = true;
}

// Archivo > Exportar > glTF binario (*.glb): toda la escena.
void exportSceneGlb(SceneNode& root, const std::vector<Material>& materials) {
    std::vector<const SceneNode*> roots;
    for (const auto& c : root.children()) roots.push_back(c.get());
    if (roots.empty()) return reportNamedError(tr("Exportar"), tr("La escena está vacía."));
    if (!nativeFileDialogAvailable())
        return reportNamedError(tr("Exportar"), tr("Exportar necesita el diálogo de archivos del sistema, que en esta plataforma todavía no existe."));
    const auto path = saveFileDialog(tr("Exportar a glTF"), {{tr("glTF binario"), "*.glb"}}, tr("Escena.glb"), "glb");
    if (!path) return;
    GltfReport grep;
    std::string err;
    if (!exportGlb(*path, roots, materials, grep, &err)) return reportFileError(*path, err);
    g.importReport = C4DImportReport{};
    g.importReport.fileName = pathUtf8(path->filename());
    g.importReport.objects = grep.nodes;
    g.importReport.meshes = grep.meshes;
    g.importReport.points = grep.points;
    g.importReport.polygons = grep.polygons;
    g.importReport.materials = grep.materials;
    g.importReport.warnings = grep.warnings;
    g.importError.clear();
    g.importTitle = tr("Exportar glTF");
    g.showImport = true;
}

void exportSceneC4D(SceneNode& root, const std::vector<Material>& materials) {
    std::vector<const SceneNode*> roots;
    for (const auto& c : root.children()) roots.push_back(c.get());
    if (roots.empty()) return reportNamedError(tr("Exportar"), tr("La escena está vacía."));
    exportC4DDialog(roots, materials, true, tr("Exportar a Cinema 4D"), tr("Escena.c4d"));
}

void exportSelectedC4D(SceneNode& root, SceneNode* selected, const std::vector<Material>& materials) {
    const auto roots = topSelected(root, selected);
    if (roots.empty()) return;
    exportC4DDialog(roots, materials, false, tr("Guardar Objetos Seleccionados como"),
                    roots.size() == 1 ? roots.front()->name() + ".c4d" : std::string(tr("Objetos.c4d")));
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

// Copiar / Cortar (Ctrl+C / Ctrl+X): guarda una copia de toda la seleccion
// (solo los de arriba; los hijos van dentro). Cortar ademas la borra.
void copySelection(SceneNode& root, SceneNode*& selected, SceneHistory& history, bool cut) {
    std::vector<SceneNode*> src = topLevelSelection(root, nullptr);
    if (src.empty() && selected && selected->parent()) src.push_back(selected);
    if (src.empty()) return;
    g.clip = std::make_unique<SceneNode>("clip");
    for (SceneNode* n : src)
        if (SceneNode* c = duplicateSubtree(*g.clip, *n)) c->setName(n->name());
    g.clipCut = cut;
    if (cut) deleteSelected(root, selected, history);
}

// Pegar (Ctrl+V): junto al objeto activo (mismo padre) o en la raiz.
void pasteClipboard(SceneNode& root, SceneNode*& selected, SceneHistory& history) {
    if (!g.clip || g.clip->children().empty()) return;
    SceneNode* parent = selected && selected->parent() ? selected->parent() : &root;
    history.commitBefore(root);
    std::vector<SceneNode*> made;
    for (const auto& ch : g.clip->children()) {
        SceneNode* c = duplicateSubtree(*parent, *ch);
        if (!c) continue;
        if (g.clipCut) c->setName(ch->name());
        made.push_back(c);
    }
    g.clipCut = false;
    if (made.empty()) return;
    selectOnly(selected, made.front());
    for (SceneNode* m : made) g.selIds.insert(m->id());
}

// Agrupar Objetos (Alt+G): un Nulo nuevo en el sitio del primero, con la
// seleccion dentro y plegado, como en C4D.
void groupObjects(SceneNode& root, SceneNode*& selected, SceneHistory& history, SceneNode* clicked) {
    const std::vector<SceneNode*> nodes = topLevelSelection(root, clicked ? clicked : selected);
    if (nodes.empty() || !nodes.front()->parent()) return;
    history.commitBefore(root);
    SceneNode* parent = nodes.front()->parent();
    SceneNode* null = parent->insertChild(std::make_unique<SceneNode>(tr("Nulo")), parent->indexOf(nodes.front()));
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

// Booleano entre los dos primeros objetos seleccionados (A = el primero de la lista, B = el segundo): crea un
// Objeto poligonal nuevo con A op B y deja A y B ocultos (se pueden volver a mostrar). Devuelve false con el motivo en `err`.
bool booleanObjects(SceneNode& root, SceneNode*& selected, SceneHistory& history, BoolOp op, std::string& err) {
    const std::vector<SceneNode*> nodes = topLevelSelection(root, selected);
    if (nodes.size() < 2) {
        err = tr("Booleano: selecciona dos objetos (A y B) con Ctrl+clic en el Gestor de Objetos.");
        return false;
    }
    SceneNode* an = nodes[0];
    SceneNode* bn = nodes[1];
    if (!an->parent()) return false;
    const glm::mat4 toA = glm::inverse(an->worldMatrix());
    auto bring = [&](SceneNode* n, MeshData& out) {
        if (!hasGeometry(n->primitive) || !nodeMeshData(*n, out)) return false;
        const glm::mat4 xf = toA * n->worldMatrix();
        const glm::mat3 nxf = glm::transpose(glm::inverse(glm::mat3(xf)));
        for (Vertex& v : out.vertices) {
            v.position = glm::vec3(xf * glm::vec4(v.position, 1.0f));
            const glm::vec3 nn = nxf * v.normal;
            v.normal = glm::length(nn) > 1e-8f ? glm::normalize(nn) : v.normal;
        }
        return true;
    };
    MeshData ma, mb, res;
    if (!bring(an, ma) || !bring(bn, mb)) {
        err = tr("Booleano: los dos objetos tienen que tener geometría (Cubo, Esfera, polígono...).");
        return false;
    }
    std::string why;
    if (!booleanMeshes(ma, mb, op, res, &why)) {
        err = tr("Booleano: ") + why;
        return false;
    }
    weldMesh(res, 1e-4f);
    updateBounds(res);
    history.commitBefore(root);
    auto made = std::make_unique<SceneNode>(an->name() + " " + tr("Booleano"), PrimitiveType::Mesh);
    made->mesh = std::make_shared<const MeshData>(std::move(res));
    made->materialIndex = an->materialIndex;
    SceneNode* parent = an->parent();
    SceneNode* result = parent->insertChild(std::move(made), parent->indexOf(an) + 1);
    result->setLocalMatrix(glm::inverse(parent->worldMatrix()) * an->worldMatrix());
    an->editorVis = VisMode::Off;
    bn->editorVis = VisMode::Off;
    selectOnly(selected, result);
    return true;
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
    m.name = materials.empty() ? tr("Mat") : tr("Mat.") + std::to_string(materials.size());
    materials.push_back(m);
    g.selectedMat = (int)materials.size() - 1;
    g.focus = AttrFocus::Material;
}

// --- Paletas de objetos (seccion 3 y 18.1 de la referencia) --------------------

struct PaletteItem {
    Tr name;
    Icon icon;
    PrimitiveType type; // None + !functional = deshabilitado
    bool functional;
    int lightKind = -1; // Luces: LightKind con el que se crea (-1 = Omni por defecto)
};

// Primitivas poligonales: se crean ya como Objeto poligonal editable (sin parametros todavia).
int paramKindFor(const std::string& key) {
    static const char* names[] = {"", "Cilindro", "Cono", "Disco", "Toroide", "Cápsula", "Tubo", "Pirámide", "Platónico"};
    for (int i = 1; i < 9; ++i)
        if (key == names[i]) return i;
    return 0;
}

// Crea el objeto de un elemento de menu/paleta; las luces salen con su tipo
// y con el nombre del menu, como en C4D ("Foco", "Luz Infinita"...).
void createPaletteItemOne(SceneNode& root, SceneNode*& selected, SceneHistory& history, const PaletteItem& it) {
    // Simetria (generador): un Nulo con SymmetryData; si hay un objeto con malla seleccionado se mete dentro.
    if (std::string(it.name.key) == "Simetría") {
        SceneNode* sel = selected;
        const bool wrap = sel && (sel->mesh || isParametric(sel->primitive)) && sel->parent();
        if (wrap) groupObjects(root, selected, history, nullptr);
        else createObject(root, selected, history, PrimitiveType::None);
        if (selected) {
            selected->setName(tr("Simetría"));
            selected->symmetry.present = true;
            g.collapsed.erase(selected->id());
        }
        return;
    }
    {
        const int pk = paramKindFor(it.name.key);
        if (pk > 0) {
            SceneNode* pn = createObject(root, selected, history, PrimitiveType::Mesh);
            if (pn) {
                pn->param = defaultParamShape(pk);
                pn->mesh = std::make_shared<const MeshData>(buildParamShape(pn->param));
                pn->param.generated = pn->mesh;
                pn->setName(tr(it.name.key));
            }
            return;
        }
    }
    if (std::string(it.name.key) == "Clonador") {
        SceneNode* sel = selected;
        const bool wrap = sel && (sel->mesh || isParametric(sel->primitive)) && sel->parent();
        SceneNode* child = wrap ? sel : nullptr;
        if (wrap) groupObjects(root, selected, history, nullptr);
        else createObject(root, selected, history, PrimitiveType::None);
        if (selected) {
            selected->setName(tr("Clonador"));
            selected->clone.present = true;
            g.collapsed.erase(selected->id());
            if (child) child->editorVis = VisMode::Off;  // las copias las dibuja el nodo
        }
        return;
    }
    if (std::string(it.name.key) == "Fondo" || std::string(it.name.key) == "Cielo") {
        const bool sky = std::string(it.name.key) == "Cielo";
        SceneNode* en = createObject(root, selected, history, PrimitiveType::None);
        if (en) {
            en->setName(tr(it.name.key));
            en->env.present = true;
            en->env.kind = sky ? 1 : 0;
            en->env.top = sky ? glm::vec3(0.25f, 0.45f, 0.85f) : glm::vec3(0.2f, 0.2f, 0.25f);
            en->env.bottom = sky ? glm::vec3(0.75f, 0.88f, 1.0f) : glm::vec3(0.2f, 0.2f, 0.25f);
        }
        return;
    }
    if (std::string(it.name.key) == "Instancia") {
        SceneNode* src = selected;
        if (!src || !src->parent() || !(src->mesh || isParametric(src->primitive))) {
            flashStatus(tr("Instancia: selecciona primero el objeto del que quieres una instancia."));
            return;
        }
        const std::string path = nodePathFromRoot(root, *src);
        SceneNode* inst = createObject(root, selected, history, PrimitiveType::None);
        if (inst) {
            inst->setName(tr("Instancia") + std::string(" ") + src->name());
            inst->instance.present = true;
            inst->instance.source = path;
            inst->position = src->position + glm::vec3(0.5f, 0.0f, 0.0f);
            inst->rotationDeg = src->rotationDeg;
            inst->scale = src->scale;
        }
        return;
    }
    if (std::string(it.name.key) == "Suelo") {
        SceneNode* fl = createObject(root, selected, history, PrimitiveType::Plane);
        if (fl) {
            fl->setName(tr("Suelo"));
            fl->scale = glm::vec3(20.0f, 1.0f, 20.0f);
        }
        return;
    }
    {
        static const char* defNames[] = {"Doblar", "Enroscar", "Afilar", "Hinchar", "Sesgar",
                                         "Aplastar y Estirar", "Esferizar", "Derretir", "Subdivisión Superficie",
                                         "Explosión", "Morph", "FFD", "Bisel"};
        int dk = -1;
        for (int i = 0; i < 13; ++i)
            if (std::string(it.name.key) == defNames[i]) dk = i;
        if (dk >= 0) {
            SceneNode* sel = selected;
            const bool wrap = sel && (sel->mesh || isParametric(sel->primitive)) && sel->parent();
            SceneNode* child = wrap ? sel : nullptr;
            if (wrap) groupObjects(root, selected, history, nullptr);
            else createObject(root, selected, history, PrimitiveType::None);
            if (selected) {
                selected->setName(tr(it.name.key));
                selected->deform.present = true;
                selected->deform.kind = dk;
                selected->deform.amount = dk == 8 ? 2.0f : dk <= 1 ? 45.0f : dk == 9 ? 20.0f : dk == 12 ? 30.0f : 50.0f;
                g.collapsed.erase(selected->id());
                if (child) child->editorVis = VisMode::Off;  // la copia deformada la dibuja el nodo
            }
            return;
        }
    }
    SceneNode* n = createObject(root, selected, history, it.type);
    const std::string itKey = it.name.key;
    if (n && (itKey == "Cámara Objetivo" || itKey == "Luz Objetivo")) {
        // el objeto mira a un Nulo "Objetivo" (se orienta solo: evaluateInstances)
        const bool isCam = it.type == PrimitiveType::Camera;
        n->setName((const char*)it.name);
        if (isCam) n->position = glm::vec3(0.0f, 0.75f, -3.0f);
        else {
            n->light.kind = LightKind::Spot;
            n->position = glm::vec3(0.0f, 2.0f, -1.5f);
        }
        SceneNode* tg = createObject(root, selected, history, PrimitiveType::None);
        if (tg) {
            tg->setName(tr("Objetivo") + std::string(" ") + n->name());
            tg->position = glm::vec3(0.0f);
            const std::string path = nodePathFromRoot(root, *tg);
            if (isCam) n->camera.target = path;
            else n->light.target = path;
        }
        selected = n;
        return;
    }
    if (n && it.type == PrimitiveType::Light && it.lightKind >= 0) {
        n->light.kind = (LightKind)it.lightKind;
        n->setName((const char*)it.name);
        if (itKey == "Luz IES") n->light.falloff = 10;  // decaimiento cuadratico, sin perfil IES
        if (itKey == "Luz PBR") {                       // area con decaimiento fisico
            n->light.falloff = 10;
            n->light.shadow = 3;
        }
        if (std::string(it.name.key) == "Luz Solar") {  // sol: infinita, calida, con sombras suaves
            n->light.color = glm::vec3(1.0f, 0.96f, 0.85f);
            n->light.intensity = 1.2f;
            n->light.shadow = 1;
            n->rotationDeg = glm::vec3(-50.0f, -35.0f, 0.0f);  // P y H: apunta hacia abajo
        }
    }
}

// Alt+Mayus: un objeto nuevo como hijo de CADA objeto seleccionado (N jerarquias independientes). Cada uno
// es un paso de deshacer. Con un solo seleccionado equivale a Alt.
void createPaletteItem(SceneNode& root, SceneNode*& selected, SceneHistory& history, const PaletteItem& it) {
    const ImGuiIO& io = ImGui::GetIO();
    if (io.KeyAlt && io.KeyShift && !io.KeyCtrl && selected) {
        const std::vector<SceneNode*> targets = topLevelSelection(root, selected);
        if (targets.size() > 1) {
            for (SceneNode* tg : targets) {
                selectOnly(selected, tg);
                createPaletteItemOne(root, selected, history, it);
            }
            return;
        }
    }
    createPaletteItemOne(root, selected, history, it);
}

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
                createPaletteItem(root, selected, history, it);
                ImGui::CloseCurrentPopup();
            }
            const bool hov = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled);
            // Franja oscura detras del icono, como la columna de iconos de R19.
            dl->AddRectFilled(p, ImVec2(p.x + kGutterW, p.y + kItemH), hov && it.functional ? kTabOn : kIconGutter);
            icons::draw(dl, it.icon, ImVec2(p.x + 2.5f, p.y + 2), 18, it.functional);
            dl->AddText(ImVec2(p.x + kGutterW + 5, p.y + 3), it.functional ? kTextCol : kTextDim, it.name);
            if (!it.functional) ImGui::EndDisabled();
            if (hov) {
                if (it.functional) ImGui::SetTooltip(tr("%s\nAñadir Objeto %s  (Alt+Clic: como hijo del seleccionado)"), it.name, it.name);
                else ImGui::SetTooltip(tr("%s\nTodavía no implementado en Iron3D"), it.name);
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
    {"Cono", Icon::Cone, kNone, true},
    {"Cubo", Icon::Cube, PrimitiveType::Cube, true},
    {"Cilindro", Icon::Cylinder, kNone, true},
    {"Disco", Icon::Disc, kNone, true},
    {"Plano", Icon::Plane, PrimitiveType::Plane, true},
    {"Polígono", Icon::Polygon, kNone, false},
    {"Esfera", Icon::Sphere, PrimitiveType::Sphere, true},
    {"Toroide", Icon::Torus, kNone, true},
    {"Cápsula", Icon::Capsule, kNone, true},
    {"Tanque de Petróleo", Icon::OilTank, kNone, false},
    {"Tubo", Icon::Tube, kNone, true},
    {"Pirámide", Icon::Pyramid, kNone, true},
    {"Platónico", Icon::Platonic, kNone, true},
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
    {"Subdivisión Superficie", Icon::GenSubdiv, kNone, true},
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
    {"Simetría", Icon::ModSymmetry, kNone, true},
    {"Generador Python", Icon::ModPython, kNone, false},
    {"LOD", Icon::ModLod, kNone, false},
    {"Reducción de Polígonos", Icon::ModPolyReduction, kNone, false},
    {"Instancia", Icon::ModInstance, kNone, true},
    {"Metaball", Icon::ModMetaball, kNone, false},
};

const PaletteItem kMoGraph[] = {
    {"Clonador", Icon::MoCloner, kNone, true},
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
    {"Doblar", Icon::DefBend, kNone, true},
    {"Hinchar", Icon::DefBulge, kNone, true},
    {"Sesgar", Icon::DefShear, kNone, true},
    {"Afilar", Icon::DefTaper, kNone, true},
    {"Enroscar", Icon::DefTwist, kNone, true},
    {"FFD", Icon::DefFfd, kNone, true},
    {"Malla", Icon::DefMesh, kNone, false},
    {"Aplastar y Estirar", Icon::DefSquash, kNone, true},
    {"Derretir", Icon::DefMelt, kNone, true},
    {"Explosión", Icon::DefExplosion, kNone, true},
    {"FX Explosión", Icon::DefExplosionFx, kNone, false},
    {"Romper", Icon::DefShatter, kNone, false},
    {"Corrección", Icon::DefCorrection, kNone, false},
    {"Jiggle", Icon::DefJiggle, kNone, false},
    {"Morph", Icon::DefMorph, kNone, true},
    {"Cubrir", Icon::DefShrinkWrap, kNone, false},
    {"Esferizar", Icon::DefSpherify, kNone, true},
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
    {"Bisel", Icon::DefBevel, kNone, true},
};

const PaletteItem kScene[] = {
    {"Suelo", Icon::Floor, PrimitiveType::Plane, true},
    {"Cielo", Icon::SceSky, kNone, true},
    {"Entorno", Icon::SceEnvironment, kNone, false},
    {"Primer Plano", Icon::SceForeground, kNone, false},
    {"Fondo", Icon::SceBackground, kNone, true},
    {"Escenario", Icon::SceStage, kNone, false},
    {"Cielo Físico", Icon::ScePhysicalSky, kNone, false},
    {"Herramienta de Nubes", Icon::SceCloudTool, kNone, false},
    {"Grupo de Nubes", Icon::SceCloudGroup, kNone, false},
    {"Nube", Icon::SceCloud, kNone, false},
    {"Unir Nubes", Icon::SceCloudJoin, kNone, false},
    {"Césped", Icon::SceGrass, kNone, false},
};

const PaletteItem kCameras[] = {
    {"Cámara", Icon::Camera, PrimitiveType::Camera, true},
    {"Cámara Objetivo", Icon::CamTarget, PrimitiveType::Camera, true},
    {"Cámara Estéreo", Icon::CamStereo, kNone, false},
    {"Cámara de Movimiento", Icon::CamMotion, kNone, false},
    {"Morph de Cámara", Icon::CamMorph, kNone, false},
    {"Grúa", Icon::CamCrane, kNone, false},
};

const PaletteItem kLights[] = {
    {"Luz", Icon::Light, PrimitiveType::Light, true},
    {"Luz Objetivo", Icon::LgtTarget, PrimitiveType::Light, true, (int)LightKind::Spot},
    {"Foco", Icon::LgtSpot, PrimitiveType::Light, true, (int)LightKind::Spot},
    {"Luz IES", Icon::LgtIes, PrimitiveType::Light, true, (int)LightKind::Spot},
    {"Luz Infinita", Icon::LgtInfinite, PrimitiveType::Light, true, (int)LightKind::Infinite},
    {"Luz de Área", Icon::LgtArea, PrimitiveType::Light, true, (int)LightKind::Area},
    {"Luz Solar", Icon::LgtSun, PrimitiveType::Light, true, (int)LightKind::Infinite},
    {"Luz PBR", Icon::LgtPbr, PrimitiveType::Light, true, (int)LightKind::Area},
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
        if (pressed(ImGuiKey_S) && shift) {
            vp.snap = !vp.snap;
            flashStatus(vp.snap ? tr("Fijación: activada") : tr("Fijación: desactivada"));
        }
        if (pressed(ImGuiKey_W) && !shift) {
            vp.worldCoords = !vp.worldCoords;
            flashStatus(vp.worldCoords ? tr("Sistema de coordenadas: Mundo") : tr("Sistema de coordenadas: Objeto"));
        }
        if (pressed(ImGuiKey_L) && !shift) {
            vp.axisMode = !vp.axisMode;
            flashStatus(vp.axisMode ? tr("Activar Eje: se mueve el eje, no el objeto") : tr("Activar Eje: desactivado"));
        }
        if (pressed(ImGuiKey_T)) { mode = GizmoMode::Scale; vp.selectTool = false; }
        // Herramientas de modelado: D Extruir, I Extrusion Interna, M~S Biselar, K Cuchillo.
        if (pressed(ImGuiKey_M)) g.chordM = ImGui::GetFrameCount();
        const bool chord = ImGui::GetFrameCount() - g.chordM < 90 && !pressed(ImGuiKey_M);
        if (!shift && pressed(ImGuiKey_D)) setModelTool(UiState::ModelTool::Extrude);
        if (!shift && pressed(ImGuiKey_I)) setModelTool(UiState::ModelTool::InnerExtrude);
        if (!shift && pressed(ImGuiKey_K)) setModelTool(UiState::ModelTool::Knife);
        if (chord && pressed(ImGuiKey_S)) { setModelTool(UiState::ModelTool::Bevel); g.chordM = -1000; }
        if (pressed(ImGuiKey_Escape) && g.modelTool != UiState::ModelTool::None) setModelTool(UiState::ModelTool::None);
        if (pressed(ImGuiKey_E) || pressed(ImGuiKey_T) || (pressed(ImGuiKey_R) && !shift)) g.modelTool = UiState::ModelTool::None;
        if (pressed(ImGuiKey_9)) { vp.selectTool = true; vp.selectShape = 0; }
        if (pressed(ImGuiKey_0)) { vp.selectTool = true; vp.selectShape = 1; }
        if (pressed(ImGuiKey_8)) { vp.selectTool = true; vp.selectShape = 2; }
        if (pressed(ImGuiKey_H)) {
            if (shift) camera.resetView();
            else frameNodes(camera, root, false);
        }
        if (pressed(ImGuiKey_O) && selected) frameNodes(camera, *selected, true);
        if (pressed(ImGuiKey_S) && !shift && !io.KeyAlt && !chord) g.pendingCmd = UiCmd::FrameSelected; // Encuadrar Seleccionados
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
            } else if ((g.timelineHovered || g.curvesHovered) && !g.keySel.empty()) {
                g.keyReq = UiState::KeyReq::Delete;
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
        // Ctrl+Mayus+Z / Ctrl+Mayus+Y = Deshacer/Rehacer Vista (como Cinema 4D); Ctrl+Y rehace la escena.
        if (shift && pressed(ImGuiKey_Z)) stepViewHistory(camera, false);
        else if (shift && pressed(ImGuiKey_Y)) stepViewHistory(camera, true);
        else if (pressed(ImGuiKey_Y)) {
            if (history.redo(root)) selected = nullptr;
        }
        if (pressed(ImGuiKey_C) && selected) copySelection(root, selected, history, false);
        if (pressed(ImGuiKey_X) && selected) copySelection(root, selected, history, true);
        if (pressed(ImGuiKey_A)) g.pendingCmd = UiCmd::SelectAll;  // Seleccionar Todo (objetos)
        if (pressed(ImGuiKey_I)) g.focus = AttrFocus::Project;  // Informacion del Proyecto
        if (pressed(ImGuiKey_V)) pasteClipboard(root, selected, history);
        if (pressed(ImGuiKey_R)) vp.renderView = true;
        if (pressed(ImGuiKey_B)) g.showRenderSettings = true;  // Editar Configuracion de Render
        if (pressed(ImGuiKey_N) && confirmDiscardChanges(root, history, materials, tr("crear un proyecto nuevo")))
            newScene(root, selected, history, materials);
        if (pressed(ImGuiKey_O)) {
            if (shift) mergeSceneFile(root, selected, history, materials);
            else if (confirmDiscardChanges(root, history, materials, tr("abrir otro proyecto")))
                openSceneFile(root, selected, history, materials, camera);
        }
        if (pressed(ImGuiKey_S)) saveDocument(root, history, materials, shift);
        if (pressed(ImGuiKey_E)) g.showPrefs = true;
        if (pressed(ImGuiKey_D)) { g.focus = AttrFocus::Project; }
        if (pressed(ImGuiKey_F9)) {  // Fotogramas Clave Automaticos
            g.autoKey = !g.autoKey;
            flashStatus(g.autoKey ? tr("Fotogramas Clave Automáticos: activados") : tr("Fotogramas Clave Automáticos: desactivados"));
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
            if (n == 0) flashStatus(tr("Hacer Editable: selecciona un Cubo, Esfera o Plano (los demás ya son editables o no tienen malla)."));
            else flashStatus(n == 1 ? tr("Hacer Editable: 1 objeto convertido a Objeto Poligonal.")
                                    : tr("Hacer Editable: ") + std::to_string(n) + tr(" objetos convertidos a Objeto Poligonal."));
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
        case UiCmd::SymmetryObject: {  // --ui-cmd=sym-obj: igual que Modelado > Simetria
            const PaletteItem it{tr("Simetría"), Icon::ModSymmetry, PrimitiveType::None, true};
            createPaletteItem(root, selected, history, it);
            break;
        }
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
            flashStatus(tr("Animación eliminada."));
            break;
        }
        case UiCmd::Cut: copySelection(root, selected, history, true); break;
        case UiCmd::Copy: copySelection(root, selected, history, false); break;
        case UiCmd::Paste: pasteClipboard(root, selected, history); break;
        case UiCmd::CurrentState: {
            const int n = currentStateToObject(root, selected, history);
            flashStatus(n ? tr("Estado Actual a Objeto: ") + std::to_string(n) + (n == 1 ? tr(" copia poligonal creada.") : tr(" copias poligonales creadas."))
                          : std::string(tr("Estado Actual a Objeto: la selección no tiene geometría.")));
            break;
        }
        case UiCmd::Connect:
        case UiCmd::ConnectDelete: {
            const int n = connectObjects(root, selected, history, cmd == UiCmd::ConnectDelete);
            flashStatus(n ? tr("Conectar Objetos: ") + std::to_string(n) + tr(" mallas unidas en un Objeto Polígono.")
                          : std::string(tr("Conectar Objetos: la selección no tiene geometría.")));
            break;
        }
        case UiCmd::BoolUnion:
        case UiCmd::BoolSubtract:
        case UiCmd::BoolIntersect: {
            std::string err;
            const BoolOp op = cmd == UiCmd::BoolUnion ? BoolOp::Union : cmd == UiCmd::BoolSubtract ? BoolOp::Subtract : BoolOp::Intersect;
            if (booleanObjects(root, selected, history, op, err)) flashStatus(tr("Booleano: objeto creado; A y B quedan ocultos."));
            else flashStatus(err);
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
    struct Cmd { Tr name; const char* key; std::function<void()> run; bool enabled; };
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
        {"Seleccionar Todo", tr("Ctrl+A"), [] { g.pendingCmd = UiCmd::SelectAll; }, true},
        {"Estado Actual a Objeto", "", [] { g.pendingCmd = UiCmd::CurrentState; }, hasSel},
        {"Conectar Objetos", "", [] { g.pendingCmd = UiCmd::Connect; }, hasSel},
        {"Conectar Objetos + Eliminar", "", [] { g.pendingCmd = UiCmd::ConnectDelete; }, hasSel},
        {"Encuadrar Geometría", "H", [] { g.pendingCmd = UiCmd::FrameAll; }, true},
        {"Encuadrar Todo", tr("Alt+H"), [] { g.pendingCmd = UiCmd::FrameAll; }, true},
        {"Encuadrar Seleccionados", "S", [] { g.pendingCmd = UiCmd::FrameSelected; }, hasSel},
        {"Encuadre por Defecto", tr("Mayús+H"), [] { g.pendingCmd = UiCmd::ResetView; }, true},
        {"Vista Perspectiva", "F1", [] { g.pendingCmd = UiCmd::ViewPersp; }, true},
        {"Vista Superior", "F2", [] { g.pendingCmd = UiCmd::ViewTop; }, true},
        {"Vista Derecha", "F3", [] { g.pendingCmd = UiCmd::ViewRight; }, true},
        {"Vista Frontal", "F4", [] { g.pendingCmd = UiCmd::ViewFront; }, true},
        {"Todas las Vistas", "F5", [] { g.pendingCmd = UiCmd::ViewQuad; }, true},
        {"Renderizar en Visor de Imágenes", tr("Mayús+R"), [] { g.renderRequested = true; }, true},
        {"Grabar Objetos Activos", "F9", [] { g.recordRequested = true; }, hasSel},
        {"Fotogramas Clave Automáticos", tr("Ctrl+F9"), [] { g.autoKey = !g.autoKey; }, true},
        {"Eliminar Animación del Objeto", "", [] { g.pendingCmd = UiCmd::ClearAnimation; }, hasSel},
        {"Editar Configuración de Render...", tr("Ctrl+B"), [] { g.showRenderSettings = true; }, true},
        {"Visor de Imágenes...", "", [] { g.showPictureViewer = true; }, true},
        {"Sistema de Coordenadas de Mundo", "W", [] { if (g_vp) g_vp->worldCoords = !g_vp->worldCoords; }, true},
        {"Activar Eje", "L", [] { if (g_vp) g_vp->axisMode = !g_vp->axisMode; }, true},
        {"Vista Izquierda", "", [] { g.pendingCmd = UiCmd::ViewLeft; }, true},
        {"Vista Trasera", "", [] { g.pendingCmd = UiCmd::ViewBack; }, true},
        {"Vista Inferior", "", [] { g.pendingCmd = UiCmd::ViewBottom; }, true},
        {"Agrupar Objetos", tr("Alt+G"), [] { g.pendingCmd = UiCmd::Group; }, hasSel},
        {"Seleccionar Hijos", "", [] { g.pendingCmd = UiCmd::SelectChildren; }, hasSel},
        {"Deseleccionar Todo", tr("Esc"), [&] { selected = nullptr; }, hasSel},
        {"Eliminar", tr("Supr"), [&] { deleteSelected(root, selected, history); }, hasSel},
        {"Deshacer", tr("Ctrl+Z"), [&] { if (history.undo(root)) selected = nullptr; }, true},
        {"Rehacer", tr("Ctrl+Y"), [&] { if (history.redo(root)) selected = nullptr; }, true},
        {"Cubo", "", [&] { createObject(root, selected, history, PrimitiveType::Cube); }, true},
        {"Esfera", "", [&] { createObject(root, selected, history, PrimitiveType::Sphere); }, true},
        {"Plano", "", [&] { createObject(root, selected, history, PrimitiveType::Plane); }, true},
        {"Luz", "", [&] { createObject(root, selected, history, PrimitiveType::Light); }, true},
        {"Nulo", "", [&] { createObject(root, selected, history, PrimitiveType::None); }, true},
        {"Nuevo Material", "", [&] { newMaterial(materials); }, true},
        {"Nuevo Proyecto", tr("Ctrl+N"), [&] { if (confirmDiscardChanges(root, history, materials, tr("crear un proyecto nuevo"))) newScene(root, selected, history, materials); }, true},
        {"Abrir...", tr("Ctrl+O"), [&] { if (confirmDiscardChanges(root, history, materials, tr("abrir otro proyecto"))) openSceneFile(root, selected, history, materials, camera); }, true},
        {"Fusionar...", tr("Ctrl+Mayús+O"), [&] { mergeSceneFile(root, selected, history, materials); }, true},
        {"Guardar Proyecto", tr("Ctrl+S"), [&] { saveDocument(root, history, materials, false); }, true},
        {"Renderizar Vista Activa", tr("Ctrl+R"), [&] { vp.renderView = true; }, true},
        {"Preferencias...", tr("Ctrl+E"), [] { g.showPrefs = true; }, true},
        {"Configuración del Proyecto", tr("Ctrl+D"), [] { g.focus = AttrFocus::Project; }, true},
        {"Navegador de Contenido", tr("Mayús+F8"), [] { g.browserOpen = true; }, true},
        {"Gestor de Objetos", tr("Mayús+F1"), [] { g.browserOpen = false; }, true},
        {"Reproducir Hacia Delante", "F8", [] { g.playDir = g.playDir == 1 ? 0 : 1; }, true},
        {"Detener", "F7", [] { g.playDir = 0; }, true},
        {"Ir al Inicio", tr("Mayús+F"), [] { g.frame = (float)g.minFrame; }, true},
        {"Ir al Final", tr("Mayús+G"), [] { g.frame = (float)g.maxFrame; }, true},
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
        const std::string n = fold((const char*)c.name);
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
    if (ImGui::Begin(tr("Commander"), &open, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
                                            ImGuiWindowFlags_AlwaysAutoResize)) {
        if (ImGui::IsWindowAppearing()) {
            std::snprintf(g.commanderBuf, sizeof(g.commanderBuf), "%s", g.commanderPreset.c_str());
            g.commanderPreset.clear();
            g.commanderSel = 0;
            ImGui::SetKeyboardFocusHere();
        }
        ImGui::SetNextItemWidth(-1);
        const bool enter = ImGui::InputTextWithHint("##cmdq", tr("Buscar orden..."), g.commanderBuf, sizeof(g.commanderBuf),
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
        if (hits.empty()) ImGui::TextDisabled(tr("Ninguna orden coincide."));
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

// --- Herramientas de modelado en los menus (Malla, menu contextual de componentes) ---------------
// op: "tool:knife|extrude|inner|bevel" (herramienta interactiva), una operacion de runCompOp, o "" = aun no
// existe (sale en gris). need = 3: solo en modo Poligonos.
void meshItem(const char* label, const char* key, const char* op, const SceneNode* selected, int need = 0) {
    using MT = UiState::ModelTool;
    const int cm = componentModeFor(selected);
    bool enabled = op[0] != '\0';
    bool checked = false;
    MT tool = MT::None;
    if (std::strncmp(op, "tool:", 5) == 0) {
        const char* n = op + 5;
        const bool kPlane = !std::strcmp(n, "knifeplane"), kLoop = !std::strcmp(n, "knifeloop");
        tool = !std::strcmp(n, "knife") || kPlane || kLoop ? MT::Knife : !std::strcmp(n, "extrude") ? MT::Extrude
               : !std::strcmp(n, "inner") ? MT::InnerExtrude : MT::Bevel;
        enabled = editableMesh(selected) != nullptr;
        checked = g.modelTool == tool;
        if (tool == MT::Knife)  // los tres modos del cuchillo: solo el suyo sale marcado
            checked = checked && (kPlane ? g.knifePlane : kLoop ? g.knifeLoop : !g.knifePlane && !g.knifeLoop);
    } else if (enabled) {
        enabled = need == 3 ? cm == 3 : cm != 0;
    }
    if (ImGui::MenuItemEx(label, kIconGutterText, key && key[0] ? key : nullptr, checked, enabled)) {
        if (tool == MT::Knife) {
            g.knifePlane = std::strcmp(op, "tool:knifeplane") == 0;
            g.knifeLoop = std::strcmp(op, "tool:knifeloop") == 0;
        }
        if (tool != MT::None) setModelTool(tool);
        else g.compOp = op;
    }
}

void meshCreateTools(const SceneNode* s) {
    meshItem(tr("Crear Punto"), "M~A", "", s);
    meshItem(tr("Puente"), "M~B, B", "bridge", s, 3);
    meshItem(tr("Pincel"), "M~C", "", s);
    meshItem(tr("Cerrar Agujero de Polígonos"), "M~D", "close", s);
    meshItem(tr("Lápiz Polígonos"), "M~E", "", s);
    ImGui::Separator();
    meshItem(tr("Cortar Línea"), "K~K, M~K", "tool:knife", s);
    meshItem(tr("Cortar Plano"), "K~J, M~J", "tool:knifeplane", s);
    meshItem(tr("Cortar Cíclico/Trayectoria"), "K~L, M~L", "tool:knifeloop", s);
}

void meshTransformTools(const SceneNode* s) {
    meshItem(tr("Planchar"), "M~G", "relax", s);
    meshItem(tr("Magnetismo"), "M~I", "", s);
    meshItem(tr("Reflejar"), "M~H", "mirror", s);
    meshItem(tr("Puntada y Cosido"), "M~P", "", s);
    meshItem(tr("Soldar"), "M~Q", "weld", s);
    ImGui::Separator();
    meshItem(tr("Bisel"), "M~S", "tool:bevel", s);
    meshItem(tr("Extruir"), "M~T, D", "tool:extrude", s);
    meshItem(tr("Extrusión Interior"), "M~W, I", "tool:inner", s);
    meshItem(tr("Extrusión Matriz"), "M~X", "", s);
    meshItem(tr("Suavizar Desplazamiento"), "M~Y", "", s);
    ImGui::Separator();
    meshItem(tr("Mover Normal"), "M~Z", "normalmove", s);
    meshItem(tr("Escalar Normal"), "M~#", "normalscale", s);
    meshItem(tr("Rotar Normal"), "M~,", "normalrot", s);
}

void meshNormalsItems(const SceneNode* s) {
    meshItem(tr("Alinear Normales"), "U~A", "align", s);
    meshItem(tr("Invertir Normales"), "U~R", "flip", s);
}

void meshNgonItems(const SceneNode* s) {
    meshItem(tr("Triangular"), "", "triang", s);
    meshItem(tr("Destriangular..."), "U~U, U~Mayús+U", "untriang", s);
    meshItem(tr("Retriangular N-gons"), "U~G", "retringons", s);
    meshItem(tr("Eliminar N-gons"), "U~E", "rmngons", s);
}

void meshCommandsMenu(const SceneNode* s) {
    meshItem(tr("Matriz"), "", "array", s);
    meshItem(tr("Clonar"), "", "clone", s, 3);
    ImGui::Separator();
    meshItem(tr("Desconectar..."), "U~D, U~Mayús+D", "", s);
    meshItem(tr("Separar"), "U~P", "split", s, 3);
    ImGui::Separator();
    meshItem(tr("Colapso"), "U~C", "collapse", s);
    meshItem(tr("Conectar"), "M~M", "connect", s);
    meshItem(tr("Fundir"), "U~Z", "melt", s);
    meshItem(tr("Disolver"), "M~N", "dissolve", s);
    ImGui::Separator();
    meshItem(tr("Subdividir..."), "U~S, U~Mayús+S", "subdiv", s);
    meshItem(tr("Triangular"), "", "triang", s);
    meshItem(tr("Destriangular..."), "U~U, U~Mayús+U", "untriang", s);
    ImGui::Separator();
    meshItem(tr("Asignar Valor al Punto"), "M~U", "", s);
    meshItem(tr("Voltear Arista"), "M~V", "turn", s, 2);
    meshItem(tr("Aristas a Spline"), "", "", s);
    meshItem(tr("Cambiar Orden de Puntos"), "", "", s);
    ImGui::Separator();
    {
        bool gear = false;
        const int cm = componentModeFor(s);
        if (gearMenuItem(nullptr, tr("Optimizar..."), "U~O, U~Mayús+O", false, cm != 0, &gear)) g.compOp = "optimize";
        if (gear) openToolOptions((int)UiState::OptWin::Optimize);
    }
    meshItem(tr("Restaurar Escala..."), "", "resetscale", s);
    meshItem(tr("Configuración Modelado"), "Mayús+M", "modelsettings", s);
    ImGui::Separator();
    meshItem(tr("Comprobar Malla"), "", "check", s);
}

// Clic derecho en el visor con un objeto editable en modo Puntos/Aristas/Polígonos: el menu de modelado de C4D.
void drawComponentContextMenu(SceneNode& root, SceneNode*& selected, SceneHistory& history) {
    const SceneNode* s = selected;
    if (iconMenuItem(Icon::Undo, tr("Deshacer (Acción)"), tr("Mayús+Z"), false, history.canUndo()))
        if (history.undo(root)) selected = nullptr;
    if (ImGui::MenuItem(tr("Encuadrar Elementos Seleccionados"), "Alt+S, S")) g.pendingCmd = UiCmd::FrameSelected;
    ImGui::Separator();
    meshItem(tr("Crear Punto"), "M~A", "", s);
    meshItem(tr("Puente"), "M~B, B", "bridge", s, 3);
    meshItem(tr("Pincel"), "M~C", "", s);
    meshItem(tr("Cerrar Agujero de Polígonos"), "M~D", "close", s);
    meshItem(tr("Lápiz Polígonos"), "M~E", "", s);
    meshItem(tr("Disolver"), "M~N", "dissolve", s);
    meshItem(tr("Planchar"), "M~G", "relax", s);
    meshItem(tr("Cortar Línea"), "K~K, M~K", "tool:knife", s);
    meshItem(tr("Cortar Plano"), "K~J, M~J", "tool:knifeplane", s);
    meshItem(tr("Cortar Cíclico/Trayectoria"), "K~L, M~L", "tool:knifeloop", s);
    meshItem(tr("Magnetismo"), "M~I", "", s);
    meshItem(tr("Reflejar"), "M~H", "mirror", s);
    meshItem(tr("Asignar Valor al Punto"), "M~U", "", s);
    meshItem(tr("Puntada y Cosido"), "M~P", "", s);
    meshItem(tr("Soldar"), "M~Q", "weld", s);
    ImGui::Separator();
    meshItem(tr("Bisel"), "M~S", "tool:bevel", s);
    meshItem(tr("Extruir"), "M~T, D", "tool:extrude", s);
    meshItem(tr("Extrusión Interior"), "M~W, I", "tool:inner", s);
    meshItem(tr("Extrusión Matriz"), "M~X", "", s);
    meshItem(tr("Suavizar Desplazamiento"), "M~Y", "", s);
    ImGui::Separator();
    meshItem(tr("Mover Normal"), "M~Z", "normalmove", s);
    meshItem(tr("Escalar Normal"), "M~#", "normalscale", s);
    meshItem(tr("Rotar Normal"), "M~,", "normalrot", s);
    ImGui::Separator();
    meshNormalsItems(s);
    ImGui::Separator();
    meshItem(tr("Matriz"), "", "array", s);
    meshItem(tr("Clonar"), "", "clone", s, 3);
    meshItem(tr("Colapso"), "U~C", "collapse", s);
    meshItem(tr("Desconectar..."), "U~D, U~Mayús+D", "", s);
    meshItem(tr("Fundir"), "U~Z", "melt", s);
    meshItem(tr("Optimizar..."), "U~O, U~Mayús+O", "optimize", s);
    meshItem(tr("Separar"), "U~P", "split", s, 3);
    meshItem(tr("Subdividir..."), "U~S, U~Mayús+S", "subdiv", s);
    ImGui::Separator();
    meshNgonItems(s);
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
    if (componentModeFor(selected) != 0) {
        drawComponentContextMenu(root, selected, history);
        ImGui::EndPopup();
        return;
    }
    const bool hasSel = selected != nullptr;
    if (iconMenuItem(Icon::Undo, tr("Deshacer"), tr("Ctrl+Z"), false, history.canUndo()))
        if (history.undo(root)) selected = nullptr;
    ImGui::Separator();
    if (ImGui::MenuItem(tr("Encuadrar Seleccionados"), "S", false, hasSel)) g.pendingCmd = UiCmd::FrameSelected;
    if (ImGui::MenuItem(tr("Encuadrar Geometría"), "H")) g.pendingCmd = UiCmd::FrameAll;
    ImGui::Separator();
    if (iconMenuItem(Icon::MakeEditable, tr("Hacer Editable"), "C", false, canMakeEditable(root, selected)))
        g.pendingCmd = UiCmd::MakeEditable;
    if (ImGui::MenuItem(tr("Estado Actual a Objeto"), nullptr, false, hasSel)) g.pendingCmd = UiCmd::CurrentState;
    {
        bool gc = false;
        if (gearMenuItem(nullptr, tr("Conectar Objetos"), nullptr, false, hasSel, &gc)) g.pendingCmd = UiCmd::Connect;
        if (gc) openToolOptions((int)UiState::OptWin::Connect);
    }
    if (ImGui::MenuItem(tr("Conectar+Eliminar"), nullptr, false, hasSel)) g.pendingCmd = UiCmd::ConnectDelete;
    {
        const bool two = g.selIds.size() >= 2;
        if (ImGui::MenuItem(tr("Booleano: Unión (A + B)"), nullptr, false, two)) g.pendingCmd = UiCmd::BoolUnion;
        if (ImGui::MenuItem(tr("Booleano: Sustracción (A - B)"), nullptr, false, two)) g.pendingCmd = UiCmd::BoolSubtract;
        if (ImGui::MenuItem(tr("Booleano: Intersección (A y B)"), nullptr, false, two)) g.pendingCmd = UiCmd::BoolIntersect;
    }
    if (iconMenuItem(Icon::Null, tr("Agrupar Objetos"), tr("Alt+G"), false, hasSel)) g.pendingCmd = UiCmd::Group;
    ImGui::Separator();
    if (ImGui::BeginMenu(tr("Nueva Etiqueta"), false)) ImGui::EndMenu();
    if (ImGui::BeginMenu(tr("Restaurar Selección"), false)) ImGui::EndMenu();
    if (ImGui::MenuItem(tr("Seleccionar Hijos"), nullptr, false, hasSel && !selected->children().empty()))
        g.pendingCmd = UiCmd::SelectChildren;
    ImGui::Separator();
    disabledItems({{tr("Mostrar Pistas..."), ""}, {tr("Mostrar F-Curves..."), ""}, {tr("Mostrar Movimientos..."), ""}});
    ImGui::Separator();
    if (iconMenuItem(Icon::Move, tr("Mover"), "E", !vp.selectTool && mode == GizmoMode::Translate)) {
        mode = GizmoMode::Translate;
        vp.selectTool = false;
    }
    if (iconMenuItem(Icon::Scale, tr("Escalar"), "T", !vp.selectTool && mode == GizmoMode::Scale)) {
        mode = GizmoMode::Scale;
        vp.selectTool = false;
    }
    if (iconMenuItem(Icon::Rotate, tr("Rotar"), "R", !vp.selectTool && mode == GizmoMode::Rotate)) {
        mode = GizmoMode::Rotate;
        vp.selectTool = false;
    }
    ImGui::Separator();
    if (iconMenuItem(Icon::LiveSelect, tr("Selección en Vivo"), "9", vp.selectTool && vp.selectShape == 0)) { vp.selectTool = true; vp.selectShape = 0; }
    {
        bool gr = false, gl = false;
        const Icon ir = Icon::MnuRectSelect, il = Icon::MnuLassoSelect;
        if (gearMenuItem(&ir, tr("Selección Rectangular"), "0", vp.selectTool && vp.selectShape == 1, true, &gr)) { vp.selectTool = true; vp.selectShape = 1; }
        if (gearMenuItem(&il, tr("Selección con Lazo"), "8", vp.selectTool && vp.selectShape == 2, true, &gl)) { vp.selectTool = true; vp.selectShape = 2; }
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

// BeginMenu que, con --ui-cmd=menu=Malla,Comandos, abre ese menu y sus submenus (verificacion sin clics).
// Los nombres son los que se ven en pantalla (en el idioma activo).
bool beginMenuDbg(const char* label, bool enabled = true) {
    if (!g.dbgMenus.empty() && g.dbgMenus.count(label) && !ImGui::IsPopupOpen(label)) ImGui::OpenPopup(label);
    return ImGui::BeginMenu(label, enabled);
}

void drawMainMenu(SceneNode& root, SceneNode*& selected, GizmoMode& mode, SceneHistory& history,
                  std::vector<Material>& materials, OrbitalCamera& camera, ViewportState& vp) {
    if (!ImGui::BeginMainMenuBar()) return;

    if (beginMenuDbg(tr("Archivo"))) {
        if (iconMenuItem(Icon::MnuNew, tr("Nuevo"), tr("Ctrl+N")) && confirmDiscardChanges(root, history, materials, tr("crear un proyecto nuevo")))
            newScene(root, selected, history, materials);
        if (iconMenuItem(Icon::MnuOpen, tr("Abrir..."), tr("Ctrl+O")) && confirmDiscardChanges(root, history, materials, tr("abrir otro proyecto")))
            openSceneFile(root, selected, history, materials, camera);
        if (ImGui::MenuItemEx(tr("Fusionar..."), kIconGutterText, tr("Ctrl+Mayús+O")))
            mergeSceneFile(root, selected, history, materials);
        if (ImGui::MenuItemEx(tr("Volver a Archivo Guardado..."), kIconGutterText, "", false, !g.docPath.empty()) &&
            confirmDiscardChanges(root, history, materials, tr("volver al archivo guardado"))) {
            std::string err;
            const std::filesystem::path path = g.docPath;
            if (loadScene(path, root, materials, &err)) {
                selected = nullptr;
                history.reset();
                g.selectedMat = -1;
                g.cleanInit = false;
            } else {
                reportFileError(path, err);
            }
        }
        ImGui::Separator();
        // Iron3D trabaja con un solo proyecto a la vez: Cerrar y Cerrar Todos dejan un proyecto nuevo.
        if (ImGui::MenuItemEx(tr("Cerrar"), kIconGutterText, tr("Ctrl+F4")) &&
            confirmDiscardChanges(root, history, materials, tr("cerrar el proyecto")))
            newScene(root, selected, history, materials);
        if (ImGui::MenuItemEx(tr("Cerrar Todos"), kIconGutterText, tr("Ctrl+Mayús+F4")) &&
            confirmDiscardChanges(root, history, materials, tr("cerrar el proyecto")))
            newScene(root, selected, history, materials);
        ImGui::Separator();
        if (iconMenuItem(Icon::MnuSave, tr("Guardar"), tr("Ctrl+S"))) saveDocument(root, history, materials, false);
        if (iconMenuItem(Icon::MnuSaveAs, tr("Guardar como..."), tr("Ctrl+Mayús+S"))) saveDocument(root, history, materials, true);
        if (ImGui::MenuItemEx(tr("Guardar incrementalmente..."), kIconGutterText, "", false, !g.docPath.empty()))
            saveIncremental(root, history, materials);
        if (ImGui::MenuItemEx(tr("Guardar Todo"), kIconGutterText, "")) saveDocument(root, history, materials, false);
        disabledItems({{tr("Guardar Proyecto con su Contenido..."), ""}});
        if (ImGui::MenuItemEx(tr("Guardar Objetos Seleccionados como..."), kIconGutterText, nullptr, false,
                              selected != nullptr && cinewareAvailable()))
            exportSelectedC4D(root, selected, materials);
        ImGui::Separator();
        if (iconBeginMenu(Icon::MnuExport, tr("Exportar"))) {
            if (ImGui::MenuItem(tr("Cinema 4D (*.c4d)..."), nullptr, false, cinewareAvailable()))
                exportSceneC4D(root, materials);
            if (!cinewareAvailable() && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip(tr("Esta compilación no lleva el Cineware SDK de MAXON"));
            if (ImGui::MenuItem(tr("Wavefront (*.obj)..."))) exportSceneObj(root, materials);
            if (ImGui::MenuItem(tr("Valve SMD (Source) (*.smd)..."))) exportSceneSmd(root, materials);
            if (ImGui::MenuItem(tr("glTF binario (*.glb)..."))) exportSceneGlb(root, materials);
            disabledItems({{tr("FBX (*.fbx)"), ""}, {tr("Alembic (*.abc)"), ""},
                           {"STL (*.stl)", ""}, {tr("Collada 1.4 (*.dae)"), ""}});
            ImGui::EndMenu();
        }
        ImGui::BeginMenuEx(tr("Archivos recientes"), kIconGutterText, false);
        ImGui::Separator();
        if (iconMenuItem(Icon::MnuExit, tr("Salir"), tr("Alt+F4"))) vp.requestQuit = true;
        ImGui::EndMenu();
    }

    if (beginMenuDbg(tr("Edición"))) {
        if (iconMenuItem(Icon::Undo, tr("Deshacer"), tr("Ctrl+Z"), false, history.canUndo()))
            if (history.undo(root)) selected = nullptr;
        if (iconMenuItem(Icon::Redo, tr("Rehacer"), tr("Ctrl+Y"), false, history.canRedo()))
            if (history.redo(root)) selected = nullptr;
        ImGui::Separator();
        if (iconMenuItem(Icon::MnuCut, tr("Cortar"), tr("Ctrl+X"), false, selected != nullptr && selected->parent()))
            copySelection(root, selected, history, true);
        if (iconMenuItem(Icon::MnuCopy, tr("Copiar"), tr("Ctrl+C"), false, selected != nullptr)) copySelection(root, selected, history, false);
        if (iconMenuItem(Icon::MnuPaste, tr("Pegar"), tr("Ctrl+V"), false, g.clip && !g.clip->children().empty()))
            pasteClipboard(root, selected, history);
        if (iconMenuItem(Icon::MnuDelete, tr("Eliminar"), tr("Supr"), false, selected != nullptr)) deleteSelected(root, selected, history);
        ImGui::Separator();
        if (iconMenuItem(Icon::MnuSelectAll, tr("Seleccionar Todo"), tr("Ctrl+A"))) g.pendingCmd = UiCmd::SelectAll;
        if (iconMenuItem(Icon::MnuDeselect, tr("Deseleccionar Todo"), tr("Esc"), false, selected != nullptr)) selected = nullptr;
        ImGui::Separator();
        if (iconMenuItem(Icon::MnuProject, tr("Configuración del Proyecto..."), tr("Ctrl+D"))) g.focus = AttrFocus::Project;
        ImGui::MenuItemEx(tr("Escalar Proyecto..."), kIconGutterText, "", false, false);
        if (iconMenuItem(Icon::MnuPrefs, tr("Preferencias..."), tr("Ctrl+E"))) g.showPrefs = true;
        ImGui::EndMenu();
    }

    if (beginMenuDbg(tr("Crear"))) {
        if (iconBeginMenu(Icon::Cube, tr("Objeto"))) {
            for (const auto& it : kPrimitives)
                if (iconMenuItem(it.icon, it.name, nullptr, false, it.functional)) createPaletteItem(root, selected, history, it);
            ImGui::EndMenu();
        }
        auto stubMenu = [&](Icon icon, const char* name, const PaletteItem* items, int n) {
            if (iconBeginMenu(icon, name)) {
                for (int i = 0; i < n; ++i)
                    if (iconMenuItem(items[i].icon, items[i].name, nullptr, false, items[i].functional))
                        createPaletteItem(root, selected, history, items[i]);
                ImGui::EndMenu();
            }
        };
        stubMenu(Icon::Spline, tr("Spline"), kSplines, countOf(kSplines));
        stubMenu(Icon::Nurbs, tr("Generadores"), kGenerators, countOf(kGenerators));
        stubMenu(Icon::ModArray, tr("Modelado"), kModeling, countOf(kModeling));
        stubMenu(Icon::Cloner, tr("MoGraph"), kMoGraph, countOf(kMoGraph));
        stubMenu(Icon::Deformer, tr("Deformador"), kDeformers, countOf(kDeformers));
        stubMenu(Icon::Floor, tr("Escena"), kScene, countOf(kScene));
        ImGui::BeginMenuEx(tr("Física"), kIconGutterText, false);
        stubMenu(Icon::Camera, tr("Cámara"), kCameras, countOf(kCameras));
        if (iconBeginMenu(Icon::Light, tr("Luz"))) {
            for (const auto& it : kLights)
                if (iconMenuItem(it.icon, it.name, nullptr, false, it.functional)) createPaletteItem(root, selected, history, it);
            ImGui::EndMenu();
        }
        if (iconBeginMenu(Icon::MnuNewMaterial, tr("Materiales"))) {
            if (iconMenuItem(Icon::MnuNewMaterial, tr("Nuevo Material"))) newMaterial(materials);
            iconMenuItem(Icon::MnuNewPbrMaterial, tr("Nuevo Material PBR"), "", false, false);
            iconMenuItem(Icon::MnuLoadMaterial, tr("Cargar Materiales..."), "", false, false);
            ImGui::EndMenu();
        }
        ImGui::BeginMenuEx(tr("Etiqueta"), kIconGutterText, false);
        ImGui::BeginMenuEx(tr("Sonido"), kIconGutterText, false);
        ImGui::EndMenu();
    }

    if (beginMenuDbg(tr("Selección"))) {
        ImGui::BeginMenuEx(tr("Filtro de Selección"), kIconGutterText, false);
        if (iconMenuItem(Icon::LiveSelect, tr("Selección en Vivo"), "9", vp.selectTool && vp.selectShape == 0)) { vp.selectTool = true; vp.selectShape = 0; }
        {
            bool gr = false, gl = false;
            const Icon ir = Icon::MnuRectSelect, il = Icon::MnuLassoSelect;
            if (gearMenuItem(&ir, tr("Selección Rectangular"), "0", vp.selectTool && vp.selectShape == 1, true, &gr)) { vp.selectTool = true; vp.selectShape = 1; }
            if (gearMenuItem(&il, tr("Selección con Lazo"), "8", vp.selectTool && vp.selectShape == 2, true, &gl)) { vp.selectTool = true; vp.selectShape = 2; }
            if (gr) { vp.selectTool = true; vp.selectShape = 1; openToolOptions((int)UiState::OptWin::RectSel); }
            if (gl) { vp.selectTool = true; vp.selectShape = 2; openToolOptions((int)UiState::OptWin::LassoSel); }
        }
        iconMenuItem(Icon::MnuPolySelect, tr("Selección de Polígono"), "", false, false);
        ImGui::Separator();
        const int cm = componentModeFor(selected);
        if (iconMenuItem(Icon::MnuLoop, tr("Selección de Bucle"), "U~L", false, cm == 2)) g.compOp = "loop";
        if (ImGui::MenuItemEx(tr("Selección de Anillo"), kIconGutterText, "U~B", false, cm == 2)) g.compOp = "ring";
        if (ImGui::MenuItemEx(tr("Selección Contorno"), kIconGutterText, "U~Q", false, cm != 0)) g.compOp = "outline";
        if (ImGui::MenuItemEx(tr("Rellenar Selección"), kIconGutterText, "U~F", false, cm == 3)) g.compOp = "fill";
        if (ImGui::MenuItemEx(tr("Patrón: 1 de cada 2"), kIconGutterText, "", false, cm != 0)) g.compOp = "pattern2";
        if (ImGui::MenuItemEx(tr("Patrón: 1 de cada 2 (desfasado)"), kIconGutterText, "", false, cm != 0)) g.compOp = "pattern2b";
        if (ImGui::MenuItemEx(tr("Patrón: 1 de cada 3"), kIconGutterText, "", false, cm != 0)) g.compOp = "pattern3";
        ImGui::Separator();
        if (ImGui::MenuItemEx(tr("Ocultar Selección"), kIconGutterText, "", false, cm != 0)) g.compOp = "hide";
        if (ImGui::MenuItemEx(tr("Ocultar No Seleccionados"), kIconGutterText, "", false, cm != 0)) g.compOp = "hideinv";
        if (ImGui::MenuItemEx(tr("Mostrar Todo"), kIconGutterText, "", false, cm != 0)) g.compOp = "unhide";
        ImGui::Separator();
        if (iconMenuItem(Icon::MnuSelectAll, tr("Seleccionar Todo"), tr("Ctrl+A"))) g.pendingCmd = UiCmd::SelectAll;
        if (iconMenuItem(Icon::MnuDeselect, tr("Deseleccionar Todo"), tr("Esc"), false, selected != nullptr)) selected = nullptr;
        for (const auto& it : std::initializer_list<std::array<const char*, 3>>{
                 {tr("Invertir Selección"), "U~I", "invert"}, {tr("Seleccionar Conectados"), "U~W", "connected"},
                 {tr("Aumentar Selección"), "U~Y", "grow"}, {tr("Reducir Selección"), "U~K", "shrink"}})
            if (ImGui::MenuItemEx(it[0], kIconGutterText, it[1], false, cm != 0)) g.compOp = it[2];
        ImGui::EndMenu();
    }

    if (beginMenuDbg(tr("Herramientas"))) {
        if (iconMenuItem(Icon::Move, tr("Mover"), "E", !vp.selectTool && mode == GizmoMode::Translate)) { mode = GizmoMode::Translate; vp.selectTool = false; }
        if (iconMenuItem(Icon::Scale, tr("Escalar"), "T", !vp.selectTool && mode == GizmoMode::Scale)) { mode = GizmoMode::Scale; vp.selectTool = false; }
        if (iconMenuItem(Icon::Rotate, tr("Rotar"), "R", !vp.selectTool && mode == GizmoMode::Rotate)) { mode = GizmoMode::Rotate; vp.selectTool = false; }
        ImGui::Separator();
        iconMenuItem(Icon::AxisX, tr("Eje X / Encabezado"), "X", false, false);
        iconMenuItem(Icon::AxisY, tr("Eje Y / Inclinación"), "Y", false, false);
        iconMenuItem(Icon::AxisZ, tr("Eje Z / Alabeo"), "Z", false, false);
        if (g_vp && iconMenuItem(Icon::CoordSystem, tr("Sistema de Coordenadas de Mundo"), "W", g_vp->worldCoords))
            g_vp->worldCoords = !g_vp->worldCoords;
        ImGui::Separator();
        ImGui::MenuItemEx(tr("Medir y Construir"), kIconGutterText, "", false, false);
        ImGui::MenuItemEx(tr("Desplazamiento"), kIconGutterText, "", false, false);
        ImGui::BeginMenuEx(tr("Organizar Objetos"), kIconGutterText, false);
        ImGui::BeginMenuEx(tr("Centrar"), kIconGutterText, false);
        ImGui::EndMenu();
    }

    auto stubTop = [](const char* name, std::initializer_list<std::pair<const char*, const char*>> items) {
        if (ImGui::BeginMenu(name)) {
            disabledItems(items);
            ImGui::EndMenu();
        }
    };
    if (beginMenuDbg(tr("Malla"))) {
        const SceneNode* ms = selected;
        if (beginMenuDbg(tr("Conversión"))) {
            if (iconMenuItem(Icon::MakeEditable, tr("Hacer Editable"), "C", false, canMakeEditable(root, selected)))
                g.pendingCmd = UiCmd::MakeEditable;
            if (ImGui::MenuItem(tr("Estado Actual a Objeto"), nullptr, false, selected != nullptr)) g.pendingCmd = UiCmd::CurrentState;
            {
                bool gc = false;
                const Icon ic = Icon::ModConnect;
                if (gearMenuItem(&ic, tr("Conectar Objetos"), nullptr, false, selected != nullptr, &gc)) g.pendingCmd = UiCmd::Connect;
                if (gc) openToolOptions((int)UiState::OptWin::Connect);
            }
            if (ImGui::MenuItem(tr("Conectar Objetos + Eliminar"), nullptr, false, selected != nullptr)) g.pendingCmd = UiCmd::ConnectDelete;
            ImGui::EndMenu();
        }
        if (beginMenuDbg(tr("Comandos"))) {
            meshCommandsMenu(ms);
            ImGui::EndMenu();
        }
        ImGui::Separator();
        if (beginMenuDbg(tr("Herramientas Crear"))) {
            meshCreateTools(ms);
            ImGui::EndMenu();
        }
        if (beginMenuDbg(tr("Herramientas Transformar"))) {
            meshTransformTools(ms);
            ImGui::EndMenu();
        }
        ImGui::Separator();
        if (beginMenuDbg(tr("Spline"))) {
            disabledItems({{tr("Interpolación Dura"), ""}, {tr("Interpolación Suave"), ""}, {nullptr, nullptr},
                           {tr("Igualar Longitud de Tangentes"), ""}, {tr("Igualar Dirección de Tangentes"), ""}, {nullptr, nullptr},
                           {tr("Unir Segmento"), ""}, {tr("Romper Segmento"), ""}, {tr("Explotar Segmentos"), ""}, {nullptr, nullptr},
                           {tr("Asignar Primer Punto"), ""}, {tr("Invertir Secuencia"), ""}, {tr("Mover Secuencia Abajo"), ""},
                           {tr("Mover Secuencia Arriba"), ""}, {nullptr, nullptr},
                           {tr("Suavizado Spline"), ""}, {tr("Bisel"), ""}, {tr("Contornear"), ""}, {tr("Sección Transversal"), ""},
                           {tr("Alinear"), ""}, {tr("Proyectar"), ""}, {tr("Redondear"), ""}, {nullptr, nullptr},
                           {tr("Sustraer Spline"), ""}, {tr("Unión Spline"), ""}, {tr("Spline Y"), ""}, {tr("Spline O"), ""},
                           {tr("Intersección Spline"), ""}});
            ImGui::EndMenu();
        }
        if (beginMenuDbg(tr("N-Gons"))) {
            meshNgonItems(ms);
            ImGui::EndMenu();
        }
        if (beginMenuDbg(tr("Normales"))) {
            meshNormalsItems(ms);
            ImGui::Separator();
            meshItem(tr("Mover Normal"), "M~Z", "normalmove", ms);
            meshItem(tr("Escalar Normal"), "M~#", "normalscale", ms);
            meshItem(tr("Rotar Normal"), "M~,", "normalrot", ms);
            ImGui::EndMenu();
        }
        if (beginMenuDbg(tr("Centrar Ejes"))) {
            disabledItems({{tr("Centrar Eje a Objeto"), ""}, {tr("Centrar Eje a Selección"), ""}});
            ImGui::EndMenu();
        }
        ImGui::Separator();
        if (beginMenuDbg(tr("Simetría de edición"), editableMesh(selected) != nullptr)) {
            if (ImGui::MenuItem(tr("Apagada"), nullptr, g.symAxis < 0)) g.symAxis = -1;
            if (ImGui::MenuItem(tr("Plano X (izquierda/derecha)"), nullptr, g.symAxis == 0)) g.symAxis = 0;
            if (ImGui::MenuItem(tr("Plano Y (arriba/abajo)"), nullptr, g.symAxis == 1)) g.symAxis = 1;
            if (ImGui::MenuItem(tr("Plano Z (delante/detrás)"), nullptr, g.symAxis == 2)) g.symAxis = 2;
            ImGui::SetNextItemWidth(110.0f);
            ImGui::DragFloat(tr("Tolerancia (cm)"), &g.symTolCm, 0.05f, 0.01f, 20.0f, "%.2f");
            ImGui::EndMenu();
        }
        ImGui::EndMenu();
    }
    stubTop(tr("Fijación"), {{tr("Activar Fijación"), tr("Mayús+S")}, {nullptr, nullptr}, {tr("Fijación 2D"), ""},
                         {tr("Fijación 2.5D"), ""}, {tr("Fijación 3D"), ""}, {nullptr, nullptr}, {tr("Plano de Trabajo"), ""}});

    if (beginMenuDbg(tr("Animación"))) {
        if (iconMenuItem(Icon::Record, tr("Grabar Objetos Activos"), "F9")) g.recordRequested = true;
        iconMenuItem(Icon::AutoKey, tr("Fotogramas Clave Automáticos"), tr("Ctrl+F9"), &g.autoKey);
        if (iconMenuItem(Icon::PrevKey, tr("Ir a la Clave Anterior"), tr("Ctrl+F"), false, !g.selKeys.empty())) jumpToKey(-1);
        if (iconMenuItem(Icon::NextKey, tr("Ir a la Clave Siguiente"), tr("Ctrl+G"), false, !g.selKeys.empty())) jumpToKey(1);
        if (ImGui::MenuItemEx(tr("Editor de Curvas..."), kIconGutterText, nullptr, g.showCurves)) g.showCurves = !g.showCurves;
        if (beginMenuDbg(tr("Claves Seleccionadas"), !g.keySel.empty())) {
            drawKeyMenuItems();
            ImGui::EndMenu();
        }
        if (ImGui::MenuItemEx(tr("Eliminar Animación del Objeto"), kIconGutterText, nullptr, false, !g.selKeys.empty()))
            g.pendingCmd = UiCmd::ClearAnimation;
        ImGui::Separator();
        if (iconMenuItem(Icon::Play, tr("Reproducir Hacia Delante"), "F8", g.playDir == 1)) g.playDir = g.playDir == 1 ? 0 : 1;
        if (iconMenuItem(Icon::PlayBack, tr("Reproducir Hacia Atrás"), "F6", g.playDir == -1)) g.playDir = g.playDir == -1 ? 0 : -1;
        if (iconMenuItem(Icon::Pause, tr("Parar"), "F7")) g.playDir = 0;
        ImGui::Separator();
        if (iconMenuItem(Icon::ToStart, tr("Ir al Inicio"), tr("Mayús+F"))) g.frame = (float)g.minFrame;
        if (iconMenuItem(Icon::ToEnd, tr("Ir al Final"), tr("Mayús+G"))) g.frame = (float)g.maxFrame;
        if (iconMenuItem(Icon::StepFwd, tr("Fotograma Siguiente"), "G")) g.frame = std::min((float)g.maxFrame, std::floor(g.frame) + 1);
        if (iconMenuItem(Icon::StepBack, tr("Fotograma Anterior"), "F")) g.frame = std::max((float)g.minFrame, std::ceil(g.frame) - 1);
        ImGui::Separator();
        iconMenuItem(Icon::MnuLoop, tr("Repetir (Loop)"), nullptr, &g.loop);
        ImGui::EndMenu();
    }
    if (beginMenuDbg(tr("Personaje"))) {
        if (ImGui::MenuItem(tr("Resolver IK (experimental)"))) {
            history.commitBefore(root);
            const int n = solveAllIk(root);
            flashStatus(n > 0 ? tr("IK: ") + std::to_string(n) + tr(" cadenas resueltas.")
                              : tr("IK: ninguna etiqueta IK activa con Punta y Objetivo alcanzables."));
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(tr("Mueve las articulaciones de cada etiqueta IK para que la punta llegue a su objetivo"));
        disabledItems({{tr("Articulación"), ""}, {tr("Piel"), ""}, {tr("Pesos"), ""}, {tr("Restricción"), ""}});
        ImGui::EndMenu();
    }
    stubTop(tr("Simulación"), {{tr("Partículas"), ""}, {tr("Thinking Particles"), ""}, {tr("Dinámicas"), ""}, {tr("Cloth"), ""},
                           {tr("Hair"), ""}});
    if (beginMenuDbg(tr("Render"))) {
        {
            bool g1 = false;
            const Icon iv = Icon::RenderView;
            if (gearMenuItem(&iv, tr("Renderizar Vista Activa"), tr("Ctrl+R"), false, true, &g1)) vp.renderView = true;
            if (g1) g.showRenderSettings = true;
        }
        iconMenuItem(Icon::MnuRenderRegion, tr("Renderizar Región"), tr("Alt+R"), false, false);
        ImGui::MenuItemEx(tr("Renderizar Seleccionados en Visor de Imágenes"), kIconGutterText, "", false, false);
        {
            bool g2 = false;
            const Icon ip = Icon::RenderPicture;
            if (gearMenuItem(&ip, tr("Renderizar en Visor de Imágenes"), tr("Mayús+R"), false, true, &g2)) g.renderRequested = true;
            if (g2) g.showRenderSettings = true;
        }
        iconMenuItem(Icon::MnuInteractiveRegion, tr("Región de Render Interactivo"), tr("Alt+R"), false, false);
        iconMenuItem(Icon::MnuMakePreview, tr("Crear Previsualización..."), tr("Alt+B"), false, false);
        ImGui::Separator();
        ImGui::MenuItemEx(tr("Añadir a la Cola de Render..."), kIconGutterText, "", false, false);
        iconMenuItem(Icon::MnuRenderQueue, tr("Cola de Render..."), "", false, false);
        ImGui::Separator();
        if (iconMenuItem(Icon::RenderSettings, tr("Editar Configuración de Render..."), tr("Ctrl+B"))) g.showRenderSettings = true;
        if (iconMenuItem(Icon::MnuPictureViewer, tr("Visor de Imágenes..."), "")) g.showPictureViewer = true;
        ImGui::EndMenu();
    }
    if (beginMenuDbg(tr("Esculpir"))) {
        meshItem(tr("Subdividir"), "", "", selected);
        disabledItems({{tr("Reducir"), ""}, {tr("Incrementar"), ""}, {nullptr, nullptr}});
        if (beginMenuDbg(tr("Pinceles"))) {
            disabledItems({{tr("Estirar"), ""}, {tr("Agarrar"), ""}, {tr("Suavizar"), ""}, {tr("Encerar"), ""}, {tr("Cortar"), ""},
                           {tr("Pellizcar"), ""}, {tr("Achatar"), ""}, {tr("Inflar"), ""}, {tr("Amplificar"), ""},
                           {tr("Rellenar"), ""}, {tr("Repetir"), ""}, {tr("Raspar"), ""}, {nullptr, nullptr},
                           {tr("Borrar"), ""}, {tr("Seleccionar"), ""}});
            ImGui::EndMenu();
        }
        ImGui::BeginMenuEx(tr("Máscara"), kIconGutterText, false);
        ImGui::Separator();
        disabledItems({{tr("Bakear Objeto Esculpido"), ""}, {tr("Simetría Esculpir"), ""}, {tr("Proyectar Malla"), ""},
                       {tr("Des-Subdividir"), ""}, {tr("Esculpir a Pose Morph"), ""}});
        ImGui::EndMenu();
    }
    stubTop(tr("Motion Tracker"), {{tr("Motion Tracker"), ""}, {tr("Rastreador de Objetos"), ""}});
    stubTop(tr("MoGraph"), {{tr("Ocultar Seleccionado"), ""}, {tr("Intercambiar Clonador-Matriz"), ""}, {tr("Selección MoGraph"), ""},
                        {tr("Pincel Pesos MoGraph"), ""}, {nullptr, nullptr}, {tr("Clonador"), ""}, {tr("Matriz"), ""},
                        {tr("Fractura"), ""}, {tr("Fractura Voronoi"), ""}, {tr("MoText"), ""}, {tr("PolyFX"), ""}});
    stubTop(tr("Character"), {{tr("Junta"), ""}, {tr("Herramienta de Junta"), ""}, {tr("Pintar Pesos"), ""}, {tr("Etiqueta IK"), ""},
                          {tr("Construcción de Personaje"), ""}});
    stubTop(tr("Línea de Trabajo"), {{tr("Sketch and Toon"), ""}, {tr("Hair"), ""}});
    stubTop(tr("Plugins"), {{tr("Ningún plugin cargado"), ""}, {nullptr, nullptr}, {tr("Buscar Plugins..."), ""}});
    stubTop(tr("Script"), {{tr("Gestor de Scripts..."), ""}, {tr("Consola..."), tr("Mayús+F10")}, {nullptr, nullptr},
                       {tr("Ejecutar Script..."), ""}});

    if (beginMenuDbg(tr("Ventana"))) {
        // Los nombres siguen al preset (Esquema / Outliner / Scene Explorer...).
        const PanelNames& n = names();
        if (iconMenuItem(Icon::MnuObjManager, n.objects, tr("Mayús+F1"), !g.browserOpen)) g.browserOpen = false;
        iconMenuItem(Icon::MnuMatManager, n.materials, tr("Mayús+F2"), true, false);
        iconMenuItem(Icon::MnuTimeline, tr(n.timeline), tr("Mayús+F3"), true, false);
        iconMenuItem(Icon::MnuAttributes, n.attributes, tr("Mayús+F5"), true, false);
        iconMenuItem(Icon::MnuCoords, n.coords, tr("Mayús+F7"), true, false);
        ImGui::Separator();
        if (iconMenuItem(Icon::MnuMatEditor, tr("Editor de Material..."), nullptr, g.showMatEditor)) g.showMatEditor = !g.showMatEditor;
        if (iconMenuItem(Icon::MnuContentBrowser, tr("Navegador de Contenido"), tr("Mayús+F8"), g.browserOpen))
            g.browserOpen = !g.browserOpen;
        iconMenuItem(Icon::MnuLayers, tr("Gestor de Capas"), tr("Mayús+F4"), false, false);
        ImGui::EndMenu();
    }
    if (beginMenuDbg(tr("Ayuda"))) {
        if (iconMenuItem(Icon::MnuHelp, tr("Mostrar Ayuda..."), "F1")) openUrl("https://staticforge.me");
        if (ImGui::MenuItemEx(tr("Mostrar Tutoriales..."), kIconGutterText, "", false, true)) openUrl("https://staticforge.me");
        if (ImGui::MenuItemEx(tr("Soporte de Static Forge..."), kIconGutterText, "", false, true)) openUrl("https://staticforge.me");
        disabledItems({{tr("Aprendizaje Online..."), ""}});
        ImGui::Separator();
        if (ImGui::MenuItemEx(tr("Comprobar Actualizaciones..."), kIconGutterText, "", false, true))
            openUrl("https://github.com/KCRooty/Iron3D-Studio/releases");
        disabledItems({{tr("Instalación Manual..."), ""}, {nullptr, nullptr}, {tr("Registro Online..."), ""}, {tr("Personalizar..."), ""}});
        if (iconMenuItem(Icon::MnuAbout, tr("Información..."))) g.showAbout = true;
        ImGui::EndMenu();
    }

    // Selector de "Entorno" a la derecha (seccion 13): de momento solo el de
    // Arranque existe en Iron3D. La etiqueta sigue al preset ("Espacio de
    // trabajo:" en Blender, "Workspace:" en Maya...).
    static const char* kLayoutsC4D[] = {tr("Arranque"), tr("Animar"), tr("Modelado"), tr("BP - UV Edit"), tr("Esculpir"), tr("Visualizar")};
    static const char* kLayoutsBlender[] = {tr("Layout"), tr("Modeling"), tr("Sculpting"), tr("UV Editing"), tr("Texture Paint"), tr("Animation")};
    const char* const* kLayouts = g_preset.layout.arrangement == Arrangement::Blender ? kLayoutsBlender : kLayoutsC4D;
    const char* layoutLabel = tr(names().layoutLabel);
    const float comboW = 140.0f;
    const float labelW = ImGui::CalcTextSize(layoutLabel).x;
    ImGui::SameLine(ImGui::GetWindowWidth() - comboW - labelW - 24.0f);
    ImGui::TextUnformatted(layoutLabel);
    ImGui::SetNextItemWidth(comboW);
    if (ImGui::BeginCombo("##layout", kLayouts[g.layoutPreset])) {
        for (int i = 0; i < 6; ++i) {
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
    if (iconButton("undo", Icon::Undo, b, false, couldUndo, tr("Deshacer [Ctrl+Z]"), tr("Deshacer la última acción.")) == BtnResult::Click)
        if (history.undo(root)) selected = nullptr;
    ImGui::SameLine();
    const bool couldRedo = history.canRedo();
    if (iconButton("redo", Icon::Redo, b, false, couldRedo, tr("Rehacer [Ctrl+Y]"), tr("Rehacer la última acción deshecha.")) == BtnResult::Click)
        if (history.redo(root)) selected = nullptr;
    ImGui::SameLine();
    toolbarSeparator(true, b);
    ImGui::SameLine();

    if (iconButton("live", Icon::LiveSelect, b, vp.selectTool, true, tr("Selección en Vivo [Atajo de Teclado 9]"),
                   tr("Selección en Vivo: Haga clic para seleccionar elementos. Pulse MAYÚSCULAS para añadir a la selección, CTRL para eliminar.")) == BtnResult::Click) {
        vp.selectTool = true;
        vp.selectShape = 0;
    }
    ImGui::SameLine();
    struct ToolDef { const char* id; Icon icon; GizmoMode m; const char* tip; const char* hint; };
    const ToolDef tools[] = {
        {"move", Icon::Move, GizmoMode::Translate, tr("Herramienta de Mover [Atajo de Teclado E]"),
         tr("Mover: Haga clic y arrastre para mover elementos. Pulse MAYÚSCULAS para cuantificar el movimiento / añadir a la selección en el modo punto, CTRL para eliminar.")},
        {"scale", Icon::Scale, GizmoMode::Scale, tr("Herramienta de Escalar [Atajo de Teclado T]"),
         tr("Escalar: Haga clic y arrastre para escalar elementos. Pulse MAYÚSCULAS para cuantificar el escalado / añadir a la selección en el modo punto, CTRL para eliminar.")},
        {"rotate", Icon::Rotate, GizmoMode::Rotate, tr("Herramienta de Rotar [Atajo de Teclado R]"),
         tr("Rotar: Haga clic y arrastre para rotar elementos. Pulse MAYÚSCULAS para cuantificar la rotación / añadir a la selección en el modo punto, CTRL para eliminar.")},
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
    if (iconButton("ax", Icon::AxisX, b, !vp.axisLock[0], true, vp.axisLock[0] ? tr("Eje X / Encabezado [X]\n(bloqueado)") : tr("Eje X / Encabezado [X]\n(desbloqueado)"),
                   tr("Bloquea o desbloquea el eje X: un eje bloqueado no cambia al mover, rotar o escalar.")) == BtnResult::Click)
        vp.axisLock[0] = !vp.axisLock[0];
    ImGui::SameLine();
    if (iconButton("ay", Icon::AxisY, b, !vp.axisLock[1], true, vp.axisLock[1] ? tr("Eje Y / Inclinación [Y]\n(bloqueado)") : tr("Eje Y / Inclinación [Y]\n(desbloqueado)"),
                   tr("Bloquea o desbloquea el eje Y: un eje bloqueado no cambia al mover, rotar o escalar.")) == BtnResult::Click)
        vp.axisLock[1] = !vp.axisLock[1];
    ImGui::SameLine();
    if (iconButton("az", Icon::AxisZ, b, !vp.axisLock[2], true, vp.axisLock[2] ? tr("Eje Z / Alabeo [Z]\n(bloqueado)") : tr("Eje Z / Alabeo [Z]\n(desbloqueado)"),
                   tr("Bloquea o desbloquea el eje Z: un eje bloqueado no cambia al mover, rotar o escalar.")) == BtnResult::Click)
        vp.axisLock[2] = !vp.axisLock[2];
    ImGui::SameLine();
    if (iconButton("coord", Icon::CoordSystem, b, vp.worldCoords, true,
                   vp.worldCoords ? tr("Usar Sistema de Coordenadas de Objeto [W]\n(ahora: Mundo)")
                                  : tr("Usar Sistema de Coordenadas de Mundo [W]\n(ahora: Objeto)"),
                   tr("Cambia los ejes del gizmo entre los del objeto y los del mundo.")) == BtnResult::Click)
        vp.worldCoords = !vp.worldCoords;
    ImGui::SameLine();
    toolbarSeparator(true, b);
    ImGui::SameLine();

    if (iconButton("rview", Icon::RenderView, b, false, true,
                   tr("Renderizar Vista Activa\nRenderiza la vista activa en el propio visor.\n[Atajo de Teclado Ctrl+R]"),
                   tr("Renderiza la vista activa en el propio visor.")) == BtnResult::Click)
        vp.renderView = true;
    ImGui::SameLine();
    if (iconButton("rpv", Icon::RenderPicture, b, false, true,
                   tr("Renderizar en Visor de Imágenes [Mayús+R]\nRenderiza la cámara activa con la Configuración de Render."),
                   tr("Renderiza la cámara activa en el Visor de Imágenes.")) == BtnResult::Click)
        g.renderRequested = true;
    ImGui::SameLine();
    if (iconButton("rset", Icon::RenderSettings, b, g.showRenderSettings, true, tr("Editar Configuración de Render... [Ctrl+B]"),
                   tr("Resolución, antialiasing y fondo del render.")) == BtnResult::Click)
        g.showRenderSettings = !g.showRenderSettings;
    ImGui::SameLine();
    toolbarSeparator(true, b);
    ImGui::SameLine();

    struct GroupDef { const char* id; Icon icon; const char* popup; const PaletteItem* items; int count; int cols;
                      PrimitiveType defType; bool defOk; const char* tip; };
    const GroupDef groups[] = {
        {"gcube", Icon::Cube, "##palPrim", kPrimitives, countOf(kPrimitives), 4, PrimitiveType::Cube, true,
         tr("Cubo  (mantener pulsado: más objetos)")},
        {"gspline", Icon::Spline, "##palSpline", kSplines, countOf(kSplines), 3, PrimitiveType::None, false,
         tr("Lápiz  (mantener pulsado: más splines)")},
        {"gnurbs", Icon::Nurbs, "##palGen", kGenerators, countOf(kGenerators), 2, PrimitiveType::None, false,
         tr("Subdivisión Superficie  (mantener pulsado: generadores)")},
        {"gmodel", Icon::ModArray, "##palModel", kModeling, countOf(kModeling), 2, PrimitiveType::None, false,
         tr("Matriz  (mantener pulsado: modelado)")},
        {"gdeform", Icon::Deformer, "##palDef", kDeformers, countOf(kDeformers), 4, PrimitiveType::None, false,
         tr("Doblar  (mantener pulsado: deformadores)")},
        {"gscene", Icon::Floor, "##palScene", kScene, countOf(kScene), 3, PrimitiveType::None, false,
         tr("Suelo  (mantener pulsado: escena)")},
        {"gcam", Icon::Camera, "##palCam", kCameras, countOf(kCameras), 1, PrimitiveType::None, false,
         tr("Cámara  (mantener pulsado: más cámaras)")},
        {"glight", Icon::Light, "##palLight", kLights, countOf(kLights), 3, PrimitiveType::Light, true,
         tr("Luz  (mantener pulsado: más luces)")},
    };
    for (const auto& gr : groups) {
        const BtnResult r = iconButton(gr.id, gr.icon, b, false, true, gr.tip,
                                       tr("Clic: crear el objeto. Mantener pulsado o clic derecho: abrir la paleta del grupo. Alt+Clic: crear como hijo del seleccionado."),
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
                   tr("Hacer Editable [C]\nConvierte Cubo, Esfera o Plano en un Objeto Poligonal."),
                   tr("Hacer Editable: convierte los objetos paramétricos seleccionados en Objetos Poligonales.")) ==
        BtnResult::Click)
        g.pendingCmd = UiCmd::MakeEditable;
    toolbarSeparator(false, b);
    const int cm = g_vp ? g_vp->compMode : 0;
    if (iconButton("model", Icon::ModelMode, b, cm == 0, true, tr("Usar Modo Modelo"), tr("Modo Modelo: se editan objetos enteros.")) ==
            BtnResult::Click && g_vp)
        g_vp->compMode = 0;
    static ViewportState noVpT;
    ViewportState& xvp = g_vp ? *g_vp : noVpT;
    if (iconButton("texmode", Icon::TextureMode, b, xvp.texMode, true, xvp.texMode ? tr("Salir de Modo Textura") : tr("Usar Modo Textura"),
                   tr("Modo Textura: edita la proyección, el desplazamiento y la longitud de la etiqueta de Textura del objeto seleccionado.")) == BtnResult::Click)
        xvp.texMode = !xvp.texMode;
    if (iconButton("axmode", Icon::AxisMode, b, g_vp && g_vp->axisMode, g_vp != nullptr, tr("Activar Eje [L]"),
                   tr("Modo Eje: mover, rotar o escalar cambia solo el eje del objeto; la geometría editable y los hijos se quedan en su sitio.")) ==
            BtnResult::Click &&
        g_vp)
        g_vp->axisMode = !g_vp->axisMode;
    toolbarSeparator(false, b);
    if (iconButton("pts", Icon::Points, b, cm == 1, true, tr("Usar Modo Puntos"),
                   tr("Modo Puntos: elige y mueve puntos de un objeto editable (C para hacerlo editable).")) == BtnResult::Click && g_vp)
        g_vp->compMode = 1;
    if (iconButton("edg", Icon::Edges, b, cm == 2, true, tr("Usar Modo Aristas"),
                   tr("Modo Aristas: elige y mueve aristas de un objeto editable.")) == BtnResult::Click && g_vp)
        g_vp->compMode = 2;
    if (iconButton("pol", Icon::Polygons, b, cm == 3, true, tr("Usar Modo Polígonos"),
                   tr("Modo Polígonos: elige y mueve polígonos (triángulos) de un objeto editable.")) == BtnResult::Click && g_vp)
        g_vp->compMode = 3;
    toolbarSeparator(false, b);
    static ViewportState noVp0;
    ViewportState& tvp = g_vp ? *g_vp : noVp0;
    if (iconButton("tweak", Icon::Tweak, b, tvp.tweak, true, tvp.tweak ? tr("Salir de Modo Ajuste") : tr("Modo Ajuste"),
                   tr("Modo Ajuste: sin gizmo, arrastre el objeto directamente con el ratón (usa la herramienta activa: Mover, Rotar o Escalar).")) == BtnResult::Click)
        tvp.tweak = !tvp.tweak;
    static ViewportState noVp;
    ViewportState& svp = g_vp ? *g_vp : noVp;
    if (iconButton("solo", Icon::Solo, b, svp.solo, true, svp.solo ? tr("Salir de Vista Solo") : tr("Vista Solo"),
                   tr("Vista Solo: el visor dibuja solo el objeto seleccionado y sus hijos.")) == BtnResult::Click)
        svp.solo = !svp.solo;
    toolbarSeparator(false, b);
    if (iconButton("snap", Icon::Snap, b, svp.snap, true, svp.snap ? tr("Desactivar Fijación [Mayús+S]") : tr("Activar Fijación [Mayús+S]"),
                   tr("Con la Fijación, mover salta de 100 cm en 100 cm (la cuadrícula).")) == BtnResult::Click)
        svp.snap = !svp.snap;
    if (iconButton("wplane", Icon::Workplane, b, svp.workplane, true, svp.workplane ? tr("Desactivar Plano de Trabajo") : tr("Plano de Trabajo"),
                   tr("Plano de Trabajo: la cuadrícula del visor en perspectiva toma la posición y orientación del objeto seleccionado.")) == BtnResult::Click)
        svp.workplane = !svp.workplane;
    if (iconButton("wlock", Icon::Locked, b, svp.workplaneLock, svp.workplane, svp.workplaneLock ? tr("Desbloquear Plano de Trabajo") : tr("Bloquear Plano de Trabajo"),
                   tr("Bloquear Plano de Trabajo: la cuadrícula deja de seguir la selección y se queda donde está.")) == BtnResult::Click)
        svp.workplaneLock = !svp.workplaneLock;
    ImGui::End();
    ImGui::PopStyleVar(2);
}

// --- Visor: menu propio + overlays (secciones 11, 12 y 16) --------------------------

// Historial de puntos de vista de cada visor (Vista > Deshacer/Rehacer Vista). Un cambio de camara cuenta como uno
// cuando se queda quieta unos frames (el final de un arrastre o de un giro de rueda).
struct ViewHistory {
    std::vector<OrbitalCamera::ViewState> undo, redo;
    OrbitalCamera::ViewState committed, lastFrame;
    bool init = false;
    int still = 0;
};

ViewHistory& trackView(const OrbitalCamera& camera) {
    static std::map<const OrbitalCamera*, ViewHistory> all;
    ViewHistory& h = all[&camera];
    const OrbitalCamera::ViewState now = camera.saveView();
    if (!h.init) {
        h.committed = h.lastFrame = now;
        h.init = true;
    } else if (!(now == h.committed)) {
        h.still = now == h.lastFrame ? h.still + 1 : 0;
        if (h.still >= 12) {
            h.undo.push_back(h.committed);
            if (h.undo.size() > 50) h.undo.erase(h.undo.begin());
            h.redo.clear();
            h.committed = now;
            h.still = 0;
        }
    } else {
        h.still = 0;
    }
    h.lastFrame = now;
    return h;
}

// Deshacer (redo=false) o Rehacer Vista: lo usan el menu Vista y los atajos Ctrl+Mayus+Z/Y.
void stepViewHistory(OrbitalCamera& camera, bool redo) {
    ViewHistory& h = trackView(camera);
    auto& from = redo ? h.redo : h.undo;
    if (from.empty()) return;
    (redo ? h.undo : h.redo).push_back(h.committed);
    h.committed = from.back();
    from.pop_back();
    camera.restoreView(h.committed);
}

void drawViewportHeader(SceneNode& root, SceneNode*& selected, OrbitalCamera& camera, ViewportState& vp,
                        ImVec2 pos, ImVec2 size) {
    ViewHistory& viewHist = trackView(camera);
    beginPanel("##vpheader", pos, size, ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoScrollbar);
    if (ImGui::BeginMenuBar()) {
        if (beginMenuDbg(tr("Vista"))) {
            ImGui::MenuItem(tr("Usar como Cámara de Render"), nullptr, false, false);
            ImGui::Separator();
            if (ImGui::MenuItem(tr("Deshacer Vista"), tr("Ctrl+Mayús+Z"), false, !viewHist.undo.empty())) {
                viewHist.redo.push_back(viewHist.committed);
                viewHist.committed = viewHist.undo.back();
                viewHist.undo.pop_back();
                camera.restoreView(viewHist.committed);
            }
            if (ImGui::MenuItem(tr("Rehacer Vista"), tr("Ctrl+Mayús+Y"), false, !viewHist.redo.empty())) {
                viewHist.undo.push_back(viewHist.committed);
                viewHist.committed = viewHist.redo.back();
                viewHist.redo.pop_back();
                camera.restoreView(viewHist.committed);
            }
            ImGui::Separator();
            if (ImGui::MenuItem(tr("Encuadrar Todo"), tr("Alt+H"))) frameNodes(camera, root, false);
            if (ImGui::MenuItem(tr("Encuadrar Geometría"), "H")) frameNodes(camera, root, false);
            if (ImGui::MenuItem(tr("Encuadrar Elementos Activos"), "O", false, selected != nullptr))
                frameNodes(camera, *selected, true);
            if (ImGui::MenuItem(tr("Encuadre por Defecto"), tr("Mayús+H"))) camera.resetView();
            ImGui::Separator();
            ImGui::MenuItem(tr("Redibujar"), "A", false, false);
            ImGui::EndMenu();
        }
        if (beginMenuDbg(tr("Cámaras"))) {
            beginMenuDbg(tr("Navegación"), false);
            if (beginMenuDbg(tr("Usar Cámara"))) {
                ImGui::MenuItem(tr("Cámara del Editor"), nullptr, true);
                ImGui::EndMenu();
            }
            ImGui::Separator();
            using CV = OrbitalCamera::View;
            const CV cv = camera.view();
            if (ImGui::MenuItem(tr("Perspectiva"), nullptr, cv == CV::Perspective)) selectView(camera, CV::Perspective);
            ImGui::MenuItem(tr("Paralela"), nullptr, false, false);
            if (ImGui::MenuItem(tr("Izquierda"), nullptr, cv == CV::Left)) selectView(camera, CV::Left);
            if (ImGui::MenuItem(tr("Derecha"), nullptr, cv == CV::Right)) selectView(camera, CV::Right);
            if (ImGui::MenuItem(tr("Frontal"), nullptr, cv == CV::Front)) selectView(camera, CV::Front);
            if (ImGui::MenuItem(tr("Trasera"), nullptr, cv == CV::Back)) selectView(camera, CV::Back);
            if (ImGui::MenuItem(tr("Superior"), nullptr, cv == CV::Top)) selectView(camera, CV::Top);
            if (ImGui::MenuItem(tr("Inferior"), nullptr, cv == CV::Bottom)) selectView(camera, CV::Bottom);
            disabledItems({{nullptr, nullptr}, {tr("Axonométrica"), ""}});
            ImGui::EndMenu();
        }
        if (beginMenuDbg(tr("Mostrar"))) {
            ImGui::MenuItem(tr("Sombreado Gouraud"), "N~A", true);
            disabledItems({{tr("Sombreado Gouraud (Líneas)"), "N~B"}, {tr("Sombreado Rápido"), "N~C"},
                           {tr("Sombreado Rápido (Líneas)"), "N~D"}, {tr("Sombreado Constante"), "N~E"},
                           {tr("Sombreado Constante (Líneas)"), "N~F"}, {tr("Líneas Ocultas"), "N~G"}, {tr("Líneas"), "N~H"},
                           {nullptr, nullptr}, {tr("Alambre"), "N~I"}, {tr("Isoparms"), "N~J"}, {tr("Caja"), "N~K"},
                           {tr("Esqueleto"), "N~L"}});
            ImGui::EndMenu();
        }
        if (beginMenuDbg(tr("Opciones"))) {
            beginMenuDbg(tr("Nivel de Detalle"), false);
            disabledItems({{nullptr, nullptr}, {tr("Estereoscópico"), ""}, {tr("Sombreado de Flujo de Trabajo Lineal"), ""},
                           {nullptr, nullptr}, {tr("OpenGL Mejorado"), ""}, {tr("Ruidos"), ""}, {tr("Post Efectos"), ""},
                           {tr("Sombras"), ""}, {tr("Transparencia"), ""}, {tr("Reflexiones"), ""}, {tr("SSAO"), ""},
                           {tr("Teselación"), ""}, {tr("Profundidad de Campo"), ""}, {nullptr, nullptr},
                           {tr("Ocultar Caras Posteriores"), "N~P"}, {tr("Usar Edición de Isolíneas"), tr("Alt+A")},
                           {tr("Color de Capa"), ""}, {tr("Normales de Polígonos"), ""}, {tr("Normales de Vértices"), ""},
                           {nullptr, nullptr}, {tr("Etiquetas"), "N~O"}, {tr("Texturas"), "N~Q"}, {tr("Rayos X"), "N~R"},
                           {nullptr, nullptr}, {tr("Luz por Defecto..."), ""}, {tr("Configurar..."), tr("Mayús+V")},
                           {tr("Configurar Todo..."), tr("Alt+V")}});
            ImGui::EndMenu();
        }
        if (beginMenuDbg(tr("Filtrar"))) {
            ImGui::MenuItem(tr("Cuadrícula"), nullptr, &vp.showGrid);
            ImGui::Separator();
            // El resto de filtros todavia no tienen efecto: se ven activos
            // (como en C4D por defecto) pero no se pueden cambiar.
            for (const char* f : {tr("Nulo"), tr("Polígono"), tr("Spline"), tr("Generador"), tr("Subdivisión Superficie"), tr("Deformador"),
                                  tr("Cámara"), tr("Luz"), tr("Entorno"), tr("Partícula"), tr("Otro")})
                ImGui::MenuItem(f, nullptr, true, false);
            ImGui::Separator();
            for (const char* f : {tr("Horizonte"), tr("Ejes Globales"), tr("Cajas Delimitadoras"), tr("HUD"), tr("Resaltar Componente"),
                                  tr("Ejes"), tr("Guías"), tr("Resaltar Objeto"), tr("Cruz Navegación")})
                ImGui::MenuItem(f, nullptr, true, false);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu(tr("Paneles"))) {
            if (ImGui::BeginMenu(tr("Disposición"))) {
                if (ImGui::MenuItem(tr("Vista Simple"), nullptr, !vp.quad) && vp.quad)
                    exitQuad(camera, vp, camera.view());
                disabledItems({{tr("2 Vistas Apiladas"), ""}, {tr("2 Vistas Lado a Lado"), ""}});
                if (ImGui::MenuItem(tr("4 Vistas"), nullptr, vp.quad) && !vp.quad) enterQuad(camera, vp);
                ImGui::EndMenu();
            }
            ImGui::Separator();
            {
                using CV = OrbitalCamera::View;
                const CV cv = camera.view();
                if (ImGui::MenuItem(tr("Vista 1 (Perspectiva)"), "F1", cv == CV::Perspective)) selectView(camera, CV::Perspective);
                if (ImGui::MenuItem(tr("Vista 2 (Superior)"), "F2", cv == CV::Top)) camera.setView(CV::Top);
                if (ImGui::MenuItem(tr("Vista 3 (Derecha)"), "F3", cv == CV::Right)) camera.setView(CV::Right);
                if (ImGui::MenuItem(tr("Vista 4 (Frontal)"), "F4", cv == CV::Front)) selectView(camera, CV::Front);
                if (ImGui::MenuItem(tr("Todas las Vistas"), "F5", vp.quad)) toggleQuad(camera);
            }
            ImGui::EndMenu();
        }
        ImGui::BeginMenu(tr("ProRender"), false);

        // Iconos de navegacion de la esquina: arrastrar sobre ellos mueve la
        // camara igual que Alt+clic (como en C4D).
        const float ib = ImGui::GetFrameHeight() - 2.0f;
        ImGui::SameLine(ImGui::GetWindowWidth() - 4 * (ib + 3) - 10);
        ImGuiIO& io = ImGui::GetIO();
        struct NavDef { const char* id; Icon icon; int op; const char* tip; };
        const NavDef navs[] = {
            {"npan", Icon::NavPan, 0, tr("Mover Cámara (arrastrar)")},
            {"nzoom", Icon::NavZoom, 1, tr("Zoom de Cámara (arrastrar)")},
            {"nrot", Icon::NavRotate, 2, tr("Rotar Cámara (arrastrar)")},
            {"nlay", Icon::NavLayout, 3, tr("Cambiar Vista (todavía no implementado)")},
        };
        for (const auto& n : navs) {
            iconButton(n.id, n.icon, ib, false, n.op != 3, n.tip, n.op == 3 ? tr("Multi-vista: todavía no implementado.") : tr("Arrastre sobre el icono para navegar por la vista."));
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
    // En Puntos/Aristas/Poligonos C4D no pinta la silueta naranja del objeto: se ven sus aristas.
    if (componentModeFor(sel) != 0) return;
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
            // Envolvente de los vertices reales (rodea la figura, no su caja); con mallas enormes, la caja.
            if (sel->mesh->vertices.size() <= 60000) {
                local.reserve(sel->mesh->vertices.size());
                for (const Vertex& v : sel->mesh->vertices) local.push_back(v.position);
                break;
            }
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
    const char* viewName = viewLabel(camera.view(), tr(names().viewport));
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

    const char* gridTxt = tr("Espaciado Cuadrícula : 100 cm");
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
            const std::string tipTxt = sel->name() + tr("\nEl objeto seleccionado está fuera de vista. Clic: llevar la cámara hasta él.");
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

bool omFilterActive() {
    return g.omSearch[0] != 0 || !g.omShowGeom || !g.omShowLight || !g.omShowCamera || !g.omShowOther;
}

bool omNodeMatches(const SceneNode& n) {
    const PrimitiveType t = n.primitive;
    const bool typeOk = hasGeometry(t) ? g.omShowGeom : t == PrimitiveType::Light ? g.omShowLight
                        : t == PrimitiveType::Camera ? g.omShowCamera : g.omShowOther;
    if (!typeOk) return false;
    if (!g.omSearch[0]) return true;
    std::string a = n.name(), b = g.omSearch;
    for (char& ch : a) ch = (char)std::tolower((unsigned char)ch);
    for (char& ch : b) ch = (char)std::tolower((unsigned char)ch);
    return a.find(b) != std::string::npos;
}

// Con filtro activo un objeto se muestra si cumple o si lo cumple algun descendiente (para no perder el camino).
bool omSubtreeMatches(const SceneNode& n) {
    if (omNodeMatches(n)) return true;
    for (const auto& c : n.children())
        if (omSubtreeMatches(*c)) return true;
    return false;
}

void collectRows(SceneNode& node, int depth, std::vector<TreeRow>& out) {
    const bool filtering = omFilterActive();
    for (const auto& c : node.children()) {
        if (filtering && !omSubtreeMatches(*c)) continue;
        out.push_back({c.get(), depth});
        if (!c->children().empty() && (filtering || !g.collapsed.count(c->id()))) collectRows(*c, depth + 1, out);
    }
}

void addChildMenu(SceneNode& root, SceneNode* parent, SceneNode*& selected, SceneHistory& history) {
    // Con `parent` = la raiz vale lo de siempre (Alt: hijo del seleccionado, Ctrl: hermano...); con otro padre
    // el objeto nace dentro de el. Mismos objetos que las paletas (cilindro, toroide, luces...).
    auto make = [&](const PaletteItem& it) {
        if (parent == &root) {
            createPaletteItem(root, selected, history, it);
        } else {
            selected = parent;
            g_forceChild = true;
            createPaletteItem(root, selected, history, it);
            g_forceChild = false;
        }
    };
    for (const auto& it : kPrimitives) {
        if (!it.functional) continue;
        if (ImGui::MenuItem(it.name)) make(it);
    }
    ImGui::Separator();
    for (const auto& it : kLights) {
        if (!it.functional) continue;
        if (ImGui::MenuItem(it.name)) make(it);
    }
}

void drawObjectManager(SceneNode& root, SceneNode*& selected, SceneHistory& history,
                       std::vector<Material>& materials, ImVec2 pos, ImVec2 size) {
    beginPanel("##objmgr", pos, size, ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoScrollbar);
    if (ImGui::BeginMenuBar()) {
        panelGrip(tr(names().objects));
        if (ImGui::BeginMenu(tr("Archivo"))) {
            disabledItems({{tr("Fusionar Objetos..."), ""}});
            if (ImGui::MenuItem(tr("Guardar Objetos Seleccionados como..."), nullptr, false,
                                selected != nullptr && cinewareAvailable()))
                exportSelectedC4D(root, selected, materials);
            if (ImGui::MenuItem(tr("Guardar como Preestablecido..."), nullptr, false,
                                selected != nullptr && cinewareAvailable()))
                beginSavePreset(*selected, materials);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu(tr("Editar"))) {
            if (ImGui::MenuItem(tr("Eliminar"), tr("Supr"), false, selected != nullptr)) deleteSelected(root, selected, history);
            if (ImGui::MenuItem(tr("Expandir Todo"))) g.collapsed.clear();
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu(tr("Ver"))) {
            disabledItems({{tr("Modo Árbol"), ""}, {tr("Modo Lista"), ""}, {nullptr, nullptr}, {tr("Mostrar Etiquetas"), ""}});
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu(tr("Objetos"))) {
            if (ImGui::BeginMenu(tr("Añadir"))) {
                addChildMenu(root, &root, selected, history);
                ImGui::EndMenu();
            }
            if (ImGui::MenuItem(tr("Duplicar"), nullptr, false, selected != nullptr)) {
                history.commitBefore(root);
                selected = duplicateSubtree(*selected->parent(), *selected);
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu(tr("Etiquetas"))) {
            const bool canTag = selected && hasGeometry(selected->primitive) && !materials.empty();
            if (ImGui::BeginMenu(tr("Etiqueta de Textura"), canTag)) {
                for (size_t i = 0; i < materials.size(); ++i)
                    if (ImGui::MenuItem(materials[i].name.c_str())) {
                        history.commitBefore(root);
                        selected->materialIndex = (int)i;
                    }
                ImGui::EndMenu();
            }
            ImGui::EndMenu();
        }
        ImGui::BeginMenu(tr("Favoritos"), false);
        const float ib = ImGui::GetFrameHeight() - 4.0f;
        ImGui::SameLine(ImGui::GetWindowWidth() - 2 * (ib + 4) - 8);
        if (iconButton("omsearch", Icon::Search, ib, g.omSearch[0] != 0, true, tr("Buscar por nombre"), nullptr) == BtnResult::Click)
            ImGui::OpenPopup("##omsearchpop");
        ImGui::SameLine(0, 4);
        if (iconButton("omfilter", Icon::Filter, ib, !(g.omShowGeom && g.omShowLight && g.omShowCamera && g.omShowOther), true,
                       tr("Filtrar por tipo"), nullptr) == BtnResult::Click)
            ImGui::OpenPopup("##omfilterpop");
        if (ImGui::BeginPopup("##omsearchpop")) {
            ImGui::SetNextItemWidth(180);
            if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
            ImGui::InputTextWithHint("##omq", tr("Nombre del objeto..."), g.omSearch, sizeof(g.omSearch));
            if (g.omSearch[0] && ImGui::SmallButton(tr("Borrar"))) g.omSearch[0] = 0;
            ImGui::EndPopup();
        }
        if (ImGui::BeginPopup("##omfilterpop")) {
            ImGui::Checkbox(tr("Geometría"), &g.omShowGeom);
            ImGui::Checkbox(tr("Luces"), &g.omShowLight);
            ImGui::Checkbox(tr("Cámaras"), &g.omShowCamera);
            ImGui::Checkbox(tr("Otros (Nulos, generadores...)"), &g.omShowOther);
            ImGui::EndPopup();
        }
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
    // Arrastrar y soltar objetos (hacer hijos / reordenar): lo que suelta el bucle y se aplica al final.
    int moveDragId = -1;
    SceneNode* moveTarget = nullptr;
    int moveZone = 1; // 0 = antes de la fila (hermano arriba), 1 = dentro (hijo), 2 = despues (hermano debajo)
    bool moveToRoot = false;
    auto rowNode = [&](int id) -> SceneNode* {
        for (const TreeRow& tr_ : rows)
            if (tr_.node->id() == id) return tr_.node;
        return nullptr;
    };

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
        if (rowHovered) g.hint = tr("Clic: seleccionar. Ctrl+Clic: anadir o quitar de la seleccion. Doble clic: renombrar. Clic derecho: menú contextual. Arrastre un material aquí para asignarlo.");
        if (ImGui::BeginDragDropSource(0)) {
            const int dragId = node->id();
            ImGui::SetDragDropPayload("IRON3D_NODE", &dragId, sizeof(int));
            const size_t nSel = g.selIds.count(dragId) ? topLevelSelection(root, node).size() : 1;
            if (nSel > 1) ImGui::Text(tr("%d objetos"), (int)nSel);
            else ImGui::TextUnformatted(node->name().c_str());
            ImGui::EndDragDropSource();
        }
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload(
                    "IRON3D_NODE", ImGuiDragDropFlags_AcceptBeforeDelivery | ImGuiDragDropFlags_AcceptNoDrawDefaultRect)) {
                SceneNode* dragged = rowNode(*(const int*)pl->Data);
                const float my = ImGui::GetIO().MousePos.y - p.y;
                const int zone = my < rowH * 0.25f ? 0 : (my > rowH * 0.75f ? 2 : 1);
                SceneNode* newParent = zone == 1 ? node : node->parent();
                if (dragged && newParent && SceneNode::canReparentNodes(topLevelSelection(root, dragged), *newParent)) {
                    const float lx = p.x + 4 + depth * 16.0f;
                    if (zone == 1) dl->AddRect(p, q, kAmber, 0.0f, 0, 2.0f);
                    else dl->AddLine(ImVec2(lx, zone == 0 ? p.y : q.y), ImVec2(q.x, zone == 0 ? p.y : q.y), kAmber, 2.0f);
                    if (pl->IsDelivery()) {
                        moveDragId = dragged->id();
                        moveTarget = node;
                        moveZone = zone;
                    }
                }
            }
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
            if (ImGui::BeginMenu(tr("CINEMA 4D Etiquetas"))) {
                const bool canTag = hasGeometry(node->primitive) && !materials.empty();
                if (ImGui::BeginMenu(tr("Etiqueta de Textura"), canTag)) {
                    for (size_t i = 0; i < materials.size(); ++i)
                        if (ImGui::MenuItem(materials[i].name.c_str(), nullptr, node->materialIndex == (int)i)) {
                            history.commitBefore(root);
                            node->materialIndex = (int)i;
                        }
                    ImGui::EndMenu();
                }
                if (ImGui::MenuItem(tr("Eliminar Etiqueta de Textura"), nullptr, false, node->materialIndex >= 0)) {
                    history.commitBefore(root);
                    node->materialIndex = -1;
                }
                ImGui::Separator();
                disabledItems({{tr("Fong"), ""}, {tr("Compositing"), ""}, {tr("Protección"), ""}, {tr("Mostrar"), ""},
                               {tr("Objetivo"), ""}, {tr("Restricción"), ""}});
                ImGui::EndMenu();
            }
            for (const char* fam : {tr("Character Etiquetas"), tr("Cineman Etiquetas"), tr("Etiqueta UVW"), tr("Hair Etiquetas"),
                                    tr("MoGraph Etiquetas"), tr("Motion Tracker Etiquetas"), tr("Scripting Etiquetas"),
                                    tr("Simulación Etiquetas"), tr("Sketch Etiquetas"), tr("Videocámara Etiquetas")})
                if (ImGui::BeginMenu(fam, false)) ImGui::EndMenu();
            ImGui::Separator();
            if (ImGui::BeginMenu(tr("Cargar Etiqueta Preestablecida"), false)) ImGui::EndMenu();
            if (ImGui::BeginMenu(tr("Restaurar Selección"), false)) ImGui::EndMenu();
            ImGui::Separator();
            iconMenuItem(Icon::MnuLayers, tr("Añadir a una Capa Nueva"), nullptr, false, false);
            iconMenuItem(Icon::MnuLayers, tr("Gestor de Capas..."), tr("Mayús+F4"), false, false);
            ImGui::Separator();
            disabledItems({{tr("Eliminar de Todas las Tomas"), ""}, {tr("Gestor de Tomas..."), ""}});
            ImGui::Separator();
            if (ImGui::MenuItem(tr("Seleccionar Hijos"), nullptr, false, !node->children().empty())) {
                forSubtree(*node, [](SceneNode& n) { g.selIds.insert(n.id()); });
                selected = node;
                g.lastSelectedId = node->id();
                g.focus = AttrFocus::Object;
            }
            ImGui::MenuItem(tr("Establecer como Raíz"), nullptr, false, false);
            ImGui::Separator();
            if (ImGui::MenuItem(tr("Expandir Todo"), nullptr, false, !node->children().empty()))
                forSubtree(*node, [](SceneNode& n) { g.collapsed.erase(n.id()); });
            if (ImGui::MenuItem(tr("Contraer Todo"), nullptr, false, !node->children().empty()))
                forSubtree(*node, [](SceneNode& n) {
                    if (!n.children().empty()) g.collapsed.insert(n.id());
                });
            ImGui::Separator();
            if (iconMenuItem(Icon::MakeEditable, tr("Hacer Editable"), "C", false, canMakeEditable(root, selected, node))) {
                const int n = makeEditable(root, selected, history, node);
                if (n > 0) flashStatus(tr("Hacer Editable: ") + std::to_string(n) + (n == 1 ? tr(" objeto convertido.") : tr(" objetos convertidos.")));
            }
            if (ImGui::MenuItem(tr("Estado Actual del Objeto"))) { if (!g.selIds.count(node->id())) selectOnly(selected, node); g.pendingCmd = UiCmd::CurrentState; }
            if (ImGui::MenuItem(tr("Conectar Objetos"))) { if (!g.selIds.count(node->id())) selectOnly(selected, node); g.pendingCmd = UiCmd::Connect; }
            if (ImGui::MenuItem(tr("Conectar+Eliminar"))) { if (!g.selIds.count(node->id())) selectOnly(selected, node); g.pendingCmd = UiCmd::ConnectDelete; }
            ImGui::Separator();
            if (iconMenuItem(Icon::Null, tr("Agrupar Objetos"), tr("Alt+G"))) pendingGroup = node;
            if (ImGui::MenuItem(tr("Expandir Grupo de Objetos"), tr("Mayús+G"), false, !node->children().empty()))
                pendingDissolve = node;
            if (ImGui::MenuItem(tr("Eliminar Sin Hijos"), nullptr, false, true)) pendingDissolve = node;
            ImGui::MenuItem(tr("Convertir Selección de Objetos a XRef"), nullptr, false, false);
            ImGui::Separator();
            disabledItems({{tr("Mostrar Pistas..."), ""}, {tr("Mostrar F-Curves..."), ""}, {tr("Mostrar Movimientos..."), ""}});
            ImGui::Separator();
            if (ImGui::MenuItem(tr("Información del Objeto..."), nullptr, false, true)) g.showObjInfo = true;
            if (iconMenuItem(Icon::MnuProject, tr("Información del Proyecto"), tr("Ctrl+I"))) g.focus = AttrFocus::Project;
            ImGui::Separator();
            iconMenuItem(Icon::MnuHelp, tr("Mostrar Ayuda..."), tr("Ctrl+F1"), false, false);
            // Lo propio de Iron3D (en C4D vive en otros menus), al final.
            ImGui::Separator();
            if (ImGui::BeginMenu(tr("Añadir Hijo"))) {
                addChildMenu(root, node, selected, history);
                ImGui::EndMenu();
            }
            if (ImGui::MenuItem(tr("Renombrar"))) {
                g.renamingNodeId = node->id();
                std::snprintf(g.renameBuf, sizeof(g.renameBuf), "%s", node->name().c_str());
                g.renameFocus = true;
            }
            if (ImGui::MenuItem(tr("Duplicar"))) pendingDuplicate = node;
            if (iconMenuItem(Icon::MnuDelete, tr("Eliminar"), tr("Supr"))) pendingDelete = node;
            if (ImGui::MenuItem(tr("Guardar como Preestablecido..."), nullptr, false, cinewareAvailable()))
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
            static const char* kNames[] = {tr("Por Defecto"), tr("Activado"), tr("Apagado")};
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(tr("Visible en %s: %s\n(clic para cambiar)"), what, kNames[(int)mode]);
                g.hint = tr("Punto superior: visible en el editor. Punto inferior: visible en el render. Gris = Por Defecto (hereda del padre), verde = Activado, rojo = Apagado.");
            }
            const ImU32 col = mode == VisMode::On    ? IM_COL32(70, 200, 80, 255)
                              : mode == VisMode::Off ? IM_COL32(220, 60, 50, 255)
                                                     : IM_COL32(150, 150, 150, 255);
            dl->AddCircleFilled(ImVec2(dotC.x, dotC.y + yOff), 3.0f, col);
        };
        visDot("##visEd", node->editorVis, -4.0f, tr("Editor"));
        visDot("##visRd", node->renderVis, 4.0f, tr("Render"));
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
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(tr("Etiqueta de Textura [%s]"), m.name.c_str());
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
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("IRON3D_NODE")) {
                moveDragId = *(const int*)pl->Data;
                moveToRoot = true;
            }
            ImGui::EndDragDropTarget();
        }
        if (ImGui::BeginPopupContextItem("##emptyctx")) {
            if (ImGui::BeginMenu(tr("Añadir"))) {
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

    if (moveDragId >= 0 && (moveTarget || moveToRoot)) {
        if (SceneNode* dragged = rowNode(moveDragId)) {
            const std::vector<SceneNode*> nodes = topLevelSelection(root, dragged);
            SceneNode* par = &root;
            size_t at = root.children().size();
            if (!moveToRoot && moveTarget) {
                if (moveZone == 1) {
                    par = moveTarget;
                    at = moveTarget->children().size();
                } else if (moveTarget->parent()) {
                    par = moveTarget->parent();
                    at = par->indexOf(moveTarget) + (moveZone == 2 ? 1 : 0);
                }
            }
            if (SceneNode::canReparentNodes(nodes, *par)) {
                history.commitBefore(root);
                SceneNode::reparentNodes(nodes, *par, at);
                if (par != &root) g.collapsed.erase(par->id());
            }
        }
    }
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
    beginPanel(tr(names[0]), pos, size, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float y = pos.y + 4;
    for (int i = 0; i < count; ++i) {
        const float len = icons::textVerticalLength(tr(names[i])) + 14.0f;
        const ImVec2 a(pos.x + 1, y), b(pos.x + size.x - 1, y + len);
        ImGui::SetCursorScreenPos(a);
        ImGui::PushID(i);
        if (ImGui::InvisibleButton("##vt", ImVec2(b.x - a.x, len)) && (enabledMask & (1u << i))) clickedTab = i;
        const bool hov = ImGui::IsItemHovered();
        if (hov && i != active && !(enabledMask & (1u << i)))
            ImGui::SetTooltip(tr("%s (todavía no implementado)"), tr(names[i]));
        ImGui::PopID();
        dl->AddRectFilled(a, b, i == active ? kTabOn : kTabOff);
        dl->AddRect(a, b, kBorderDark);
        icons::textVertical(dl, ImVec2(b.x - 3, a.y + 7), i == active ? kTextCol : kTextDim, tr(names[i]));
        y += len + 2;
    }
    ImGui::End();
    return clickedTab;
}

// --- Gestor de Atributos (secciones 6, 15, 17.3, 18.2, 19.3) --------------------

void attrHeaderNav() {
    const float ib = ImGui::GetFrameHeight() - 4.0f;
    ImGui::SameLine(ImGui::GetWindowWidth() - 5 * (ib + 3) - 8);
    iconButton("anback", Icon::Back, ib, false, false, tr("Atrás"), nullptr);
    ImGui::SameLine(0, 3);
    iconButton("anfwd", Icon::Forward, ib, false, false, tr("Adelante"), nullptr);
    ImGui::SameLine(0, 3);
    iconButton("anup", Icon::Up, ib, false, false, tr("Subir"), nullptr);
    ImGui::SameLine(0, 3);
    iconButton("anlock", Icon::Lock, ib, false, false, tr("Bloquear Elemento"), nullptr);
    ImGui::SameLine(0, 3);
    iconButton("annew", Icon::NewPanel, ib, false, false, tr("Nuevo Gestor de Atributos"), nullptr);
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
    titleLine(Icon::Gear, tr("Proyecto"));
    static unsigned mask = 1u;
    const char* tabs[] = {tr("Configuración del Proyecto"), tr("Información"), tr("Dinámicas"), tr("Referenciar"), tr("A Realizar"),
                          tr("Interpolación de Claves")};
    attrTabRow(tabs, IM_ARRAYSIZE(tabs), mask);
    if (tabOn(mask, 0)) {
        sectionHeader(tr("Configuración del Proyecto"));
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
        propLabel(tr("Escala del Proyecto"), false);
        ImGui::BeginDisabled();
        ImGui::SetNextItemWidth(50);
        float one = 1.0f;
        ImGui::DragFloat("##scale", &one, 1, 0, 0, "%.0f");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(std::min(110.0f, ImGui::GetContentRegionAvail().x - 4.0f));
        if (ImGui::BeginCombo("##unit", tr("Centímetros"))) ImGui::EndCombo();
        ImGui::Button(tr("Escalar Proyecto..."), ImVec2(std::min(fullW - 8.0f, 260.0f), 0));
        ImGui::EndDisabled();
        setHint(tr("Escalar Proyecto: todavía no implementado en Iron3D."));
        ImGui::Separator();

        col(0);
        propLabel(tr("FPS"), false);
        ImGui::SetNextItemWidth(numW);
        if (ImGui::DragInt("##fps", &g.fps, 0.2f, 1, 240)) g.fps = std::clamp(g.fps, 1, 240);
        setHint(tr("Fotogramas por segundo: velocidad de reproducción de la Línea de Tiempo."));
        col(1);
        propLabel(tr("Tiempo del Proyecto"), false);
        int cur = (int)std::lround(g.frame);
        ImGui::SetNextItemWidth(numW);
        if (ImGui::DragInt("##cur", &cur, 0.2f, g.minFrame, g.maxFrame, "%d F")) g.frame = (float)cur;
        col(0);
        propLabel(tr("Tiempo Mínimo"), false);
        ImGui::SetNextItemWidth(numW);
        if (ImGui::DragInt("##min", &g.minFrame, 0.2f, -100000, g.maxFrame - 1, "%d F")) g.previewMin = std::max(g.previewMin, g.minFrame);
        col(1);
        propLabel(tr("Tiempo Máximo"), false);
        ImGui::SetNextItemWidth(numW);
        if (ImGui::DragInt("##max", &g.maxFrame, 0.2f, g.minFrame + 1, 100000, "%d F")) g.previewMax = std::min(g.previewMax, g.maxFrame);
        col(0);
        propLabel(tr("Tiempo Mínimo de Previo"), false);
        ImGui::SetNextItemWidth(numW);
        ImGui::DragInt("##pmin", &g.previewMin, 0.2f, g.minFrame, g.previewMax, "%d F");
        col(1);
        propLabel(tr("Tiempo Máximo de Previo"), false);
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
            setHint(tr("Parámetro todavía no implementado en Iron3D"));
        };
        col(0); roFloatN(tr("Nivel de Detalle"), 100.0f, "%.0f %%");
        col(1); roCheck(tr("Nivel de Detalle del Render en el Editor"), false, false);
        ImGui::Separator();
        col(0); roCheck(tr("Usar Animación"), true, false);
        col(1); roCheck(tr("Usar Expresiones"), true, false);
        col(0); roCheck(tr("Usar Generadores"), true, false);
        col(1); roCheck(tr("Usar Deformadores"), true, false);
        col(0); roCheck(tr("Usar Sistema de Movimiento"), true, false);
        ImGui::Separator();

        g_labelW = std::floor(fullW * 0.45f);
        roCombo(tr("Color Objetos por Defecto"), tr("Gris-Azul"));
        roCombo(tr("Recortar Vista"), tr("Medio"));
        roCheck(tr("Flujo de Trabajo Lineal"), true, false);
        roCombo(tr("Introducir Perfil de Color"), tr("sRGB"));
        g_labelW = savedLabelW;

        ImGui::Spacing();
        ImGui::BeginDisabled();
        const float bw = std::floor((fullW - 12.0f) * 0.5f);
        ImGui::Button(tr("Cargar Preestablecido..."), ImVec2(bw, 0));
        ImGui::SameLine();
        ImGui::Button(tr("Guardar Preestablecido..."), ImVec2(bw, 0));
        ImGui::EndDisabled();
        setHint(tr("Preestablecidos de proyecto: todavía no implementados en Iron3D."));
    }
    if (mask & ~1u) {
        sectionHeader(tr("Página no disponible"));
        ImGui::TextDisabled(tr("  Todavía no implementado en Iron3D."));
    }
}

void drawObjectAttributes(SceneNode& node, SceneNode& root, SceneHistory& history) {
    const bool isLight = node.primitive == PrimitiveType::Light;
    const bool isNull = node.primitive == PrimitiveType::None;
    const int kind = isLight ? 1 : (isNull ? 2 : 0);
    unsigned& mask = g.objMask[kind];

    char title[200];
    std::snprintf(title, sizeof(title), tr("Objeto %s [%s]"), defaultNameFor(node.primitive), node.name().c_str());
    titleLine(iconFor(node.primitive), title);

    static const char* geoTabs[] = {tr("Básico"), tr("Coord."), tr("Objeto"), tr("Phong")};
    static const char* lightTabs[] = {tr("Básico"), tr("Coord."), tr("General"), tr("Detalles"), tr("Visibilidad"), tr("Sombra"),
                                      tr("Fotométrico"), tr("Cáusticas"), tr("Ruido"), tr("Lentes"), tr("Proyecto")};
    static const char* nullTabs[] = {tr("Básico"), tr("Coord."), tr("Objeto")};
    if (isLight) attrTabRow(lightTabs, IM_ARRAYSIZE(lightTabs), mask);
    else if (isNull) attrTabRow(nullTabs, IM_ARRAYSIZE(nullTabs), mask);
    else attrTabRow(geoTabs, IM_ARRAYSIZE(geoTabs), mask);

    if (tabOn(mask, 0)) {
        sectionHeader(tr("Propiedades Básicas"));
        propLabel(tr("Nombre"), false);
        char nameBuf[128];
        std::snprintf(nameBuf, sizeof(nameBuf), "%s", node.name().c_str());
        ImGui::SetNextItemWidth(fieldWidth(220));
        if (ImGui::InputText("##name", nameBuf, sizeof(nameBuf)) && nameBuf[0]) node.setName(nameBuf);
        trackEdit(history, root);
        roCombo(tr("Capa"), "");
        const char* visItems[] = {tr("Por Defecto"), tr("Activado"), tr("Apagado")};
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
        visCombo(tr("Visible en Editor"), "##vised", node.editorVis,
                 tr("Visible en Editor (punto superior): Por Defecto hereda del padre, Activado lo muestra siempre, Apagado lo oculta en el visor."));
        visCombo(tr("Visible en Render"), "##visrd", node.renderVis,
                 tr("Visible en Render (punto inferior): Iron3D todavía no tiene render final; el valor se guarda en la escena."));
        roCombo(tr("Usar Color"), tr("Desactivado"));
        roCheck(tr("Activado"), true);
        roCheck(tr("Rayos X"), false);
    }
    if (tabOn(mask, 1)) {
        sectionHeader(tr("Coordenadas"));
        const glm::vec3 protPos = node.position, protRot = node.rotationDeg, protScl = node.scale;
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
                if (ImGui::DragFloat("##p", &cm, 1.0f, 0, 0, tr("%.0f cm"))) node.position[i] = cm / kCmPerUnit;
                if (node.protection.present) applyProtection(node, protPos, protRot, protScl); // Proteccion/Limite
                trackEdit(history, root);
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(sl[i]);
                ImGui::SameLine(44);
                ImGui::SetNextItemWidth(-1);
                ImGui::DragFloat("##s", &node.scale[i], 0.01f, 0.001f, 1000.0f, "%.3f");
                if (node.protection.present) applyProtection(node, protPos, protRot, protScl);
                trackEdit(history, root);
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(rl[i]);
                ImGui::SameLine(44);
                ImGui::SetNextItemWidth(-1);
                ImGui::DragFloat("##r", &node.rotationDeg[rotIdx[i]], 0.5f, 0, 0, "%.1f °");
                if (node.protection.present) applyProtection(node, protPos, protRot, protScl);
                trackEdit(history, root);
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        roCombo(tr("Orden"), tr("HPB"));
        if (node.protection.present) {
            // Etiqueta Proteccion/Limite de C4D: modo por eje y, en modo Limite, el rango permitido.
            sectionHeader(tr("Protección"));
            ProtectionData& pr = node.protection;
            static const char* modeNames[] = {tr("Libre"), tr("Bloquear"), tr("Límite")};
            const char* ax[3][3] = {{"P . X", "P . Y", "P . Z"}, {"S . X", "S . Y", "S . Z"}, {"R . H", "R . P", "R . B"}};
            const int rotOrder[3] = {1, 0, 2}; // fila H, P, B -> componente de rotationDeg
            bool changed = false;
            for (int ch = 0; ch < 3; ++ch)
                for (int i = 0; i < 3; ++i) {
                    const int c = ch == 2 ? rotOrder[i] : i;
                    int* mode = ch == 0 ? &pr.posMode[c] : ch == 1 ? &pr.sclMode[c] : &pr.rotMode[c];
                    glm::vec3& lo = ch == 0 ? pr.posMin : ch == 1 ? pr.sclMin : pr.rotMin;
                    glm::vec3& hi = ch == 0 ? pr.posMax : ch == 1 ? pr.sclMax : pr.rotMax;
                    const float unit = ch == 0 ? kCmPerUnit : 1.0f;
                    ImGui::PushID(ch * 3 + i);
                    ImGui::TextUnformatted(ax[ch][i]);
                    ImGui::SameLine(44);
                    ImGui::SetNextItemWidth(90);
                    if (ImGui::Combo("##pm", mode, modeNames, 3)) changed = true;
                    if (*mode == ProtectionData::Limit) {
                        float a = lo[c] * unit, b = hi[c] * unit;
                        ImGui::SameLine();
                        ImGui::SetNextItemWidth(70);
                        if (ImGui::DragFloat("##pl", &a, 0.5f, 0, 0, "%.1f")) { lo[c] = a / unit; changed = true; }
                        ImGui::SameLine();
                        ImGui::SetNextItemWidth(70);
                        if (ImGui::DragFloat("##ph", &b, 0.5f, 0, 0, "%.1f")) { hi[c] = b / unit; changed = true; }
                    }
                    ImGui::PopID();
                }
            if (ImGui::Checkbox(tr("Permitir expresiones"), &pr.allowExpressions)) changed = true;
            ImGui::TextDisabled(tr("  Bloquear y Límite actúan al editar; sin \"Permitir expresiones\" también limitan a la IK."));
            if (changed) history.commit(history.snapshot(root));
        }
        sectionHeader(tr("Congelar Transformación"));
        ImGui::TextDisabled(tr("  Todavía no implementado en Iron3D."));
    }
    if (tabOn(mask, 2)) {
        if (isLight) {
            // Pestana General de la Luz de C4D (2026-11-10): antes todo era de
            // solo lectura y cualquier luz iluminaba igual.
            LightData& l = node.light;
            sectionHeader(tr("General"));
            propLabel(tr("Color"));
            ImGui::SetNextItemWidth(fieldWidth(240));
            ImGui::ColorEdit3("##lc", &l.color.x, ImGuiColorEditFlags_DisplayHSV | ImGuiColorEditFlags_PickerHueBar);
            trackEdit(history, root);
            propLabel(tr("Intensidad"));
            float pct = l.intensity * 100.0f;
            ImGui::SetNextItemWidth(fieldWidth());
            if (ImGui::DragFloat("##li", &pct, 1.0f, 0.0f, 100000.0f, "%.0f %%")) l.intensity = std::max(pct, 0.0f) / 100.0f;
            trackEdit(history, root);
            static const char* kinds[] = {tr("Omni"), tr("Foco"), tr("Foco Cuadrado"), tr("Infinita"), tr("Paralela"), tr("Foco Paralelo"),
                                          tr("Foco Paralelo Cuadrado"), tr("Tubo"), tr("Área"), tr("IES")};
            propLabel(tr("Tipo"), false);
            int lkind = (int)l.kind;
            ImGui::SetNextItemWidth(fieldWidth());
            if (ImGui::Combo("##lkind", &lkind, kinds, IM_ARRAYSIZE(kinds))) {
                history.commitBefore(root);
                l.kind = (LightKind)lkind;
            }
            setHint(tr("Tipo de luz. En el visor, Tubo, Área e IES iluminan como una Omni; Paralela y Foco Paralelo como una Infinita."));
            static const char* shadows[] = {tr("Ninguno"), tr("Mapeado (Suave)"), tr("Trazado (Duro)"), tr("Área")};
            propLabel(tr("Sombra"), false);
            int sh = std::clamp(l.shadow, 0, 3);
            ImGui::SetNextItemWidth(fieldWidth());
            if (ImGui::Combo("##lshadow", &sh, shadows, IM_ARRAYSIZE(shadows))) {
                history.commitBefore(root);
                l.shadow = sh;
            }
            setHint(tr("Con sombra activada, la primera luz de la escena que la tenga proyecta sombras en el visor."));
            roCombo(tr("Luz Visible"), tr("Ninguno"));
            auto check = [&](const char* label, const char* id, bool& v) {
                propLabel(label);
                bool b = v;
                if (ImGui::Checkbox(id, &b)) {
                    history.commitBefore(root);
                    v = b;
                }
            };
            check(tr("No Ilumina"), "##lnoillum", l.noIllumination);
            roCheck(tr("Mostrar Iluminación"), true, false);
            check(tr("Difusión"), "##ldiff", l.diffuse);
            roCheck(tr("Mostrar Luz Visible"), false, false);
            check(tr("Especular"), "##lspec", l.specular);
            roCheck(tr("Iluminación de IG"), true);
            propLabel(tr("Objetivo"));
            char tbuf[256];
            std::snprintf(tbuf, sizeof tbuf, "%s", l.target.c_str());
            ImGui::SetNextItemWidth(-1);
            if (ImGui::InputText("##ltarget", tbuf, sizeof tbuf)) {
                if (ImGui::IsItemActivated()) history.commitBefore(root);
                l.target = tbuf;
            }
        } else if (node.primitive == PrimitiveType::Camera) {
            sectionHeader(tr("Propiedades del Objeto"));
            CameraData& cd = node.camera;
            propLabel(tr("Proyección"));
            const char* projs[] = {tr("Perspectiva"), tr("Paralela")};
            int pj = cd.projection == 0 ? 0 : 1;
            ImGui::SetNextItemWidth(-1);
            if (ImGui::Combo("##cproj", &pj, projs, 2)) {
                history.commitBefore(root);
                cd.projection = pj;
            }
            propLabel(tr("Distancia Focal"));
            float fm = cd.focalMm;
            ImGui::SetNextItemWidth(-1);
            if (ImGui::DragFloat("##cfocal", &fm, 0.5f, 1.0f, 600.0f, "%.1f mm")) {
                if (ImGui::IsItemActivated()) history.commitBefore(root);
                cd.focalMm = fm;
            }
            propLabel(tr("Tamaño del Sensor"));
            float sm = cd.sensorMm;
            ImGui::SetNextItemWidth(-1);
            if (ImGui::DragFloat("##csensor", &sm, 0.1f, 1.0f, 100.0f, "%.1f mm")) {
                if (ImGui::IsItemActivated()) history.commitBefore(root);
                cd.sensorMm = sm;
            }
            propLabel(tr("Campo de Visión"));
            ImGui::TextDisabled("%.1f °", cd.fovDeg());
            propLabel(tr("Objetivo"));
            char tbuf[256];
            std::snprintf(tbuf, sizeof tbuf, "%s", cd.target.c_str());
            ImGui::SetNextItemWidth(-1);
            if (ImGui::InputText("##ctarget", tbuf, sizeof tbuf)) {
                if (ImGui::IsItemActivated()) history.commitBefore(root);
                cd.target = tbuf;
            }
            ImGui::TextDisabled(tr("  Ruta por nombres del objeto al que mira (vacío = sin objetivo)."));
        } else if (node.deform.present) {
            sectionHeader(tr("Propiedades del Objeto"));
            propLabel(tr("Tipo"));
            const char* kinds[] = {tr("Doblar"), tr("Enroscar"), tr("Afilar"), tr("Hinchar"), tr("Sesgar"),
                                   tr("Aplastar y Estirar"), tr("Esferizar"), tr("Derretir"), tr("Subdivisión Superficie"),
                                   tr("Explosión"), tr("Morph"), tr("FFD"), tr("Bisel")};
            ImGui::SetNextItemWidth(-1);
            int kd = node.deform.kind;
            if (ImGui::Combo("##defkind", &kd, kinds, 13)) {
                history.commitBefore(root);
                node.deform.kind = kd;
                if (kd == 8) node.deform.amount = std::clamp(std::round(node.deform.amount), 1.0f, 4.0f);
            }
            if (node.deform.kind == 8) {
                propLabel(tr("Subdivisiones"));
                int lv = (int)std::lround(node.deform.amount);
                ImGui::SetNextItemWidth(-1);
                if (ImGui::SliderInt("##defsub", &lv, 1, 4)) {
                    if (ImGui::IsItemActivated()) history.commitBefore(root);
                    node.deform.amount = (float)lv;
                }
                ImGui::TextDisabled(tr("  Catmull-Clark sobre el primer hijo con malla; el hijo es la jaula."));
            } else if (node.deform.kind == 11) {
                for (int i = 0; i < 8; ++i) {
                    char lbl[48];
                    std::snprintf(lbl, sizeof lbl, "%s %c%c%c", tr("Esquina"), (i & 1) ? '+' : '-', (i & 2) ? '+' : '-', (i & 4) ? '+' : '-');
                    propLabel(lbl);
                    ImGui::SetNextItemWidth(-1);
                    char id[16];
                    std::snprintf(id, sizeof id, "##ffd%d", i);
                    if (ImGui::DragFloat3(id, &node.deform.ffd[i].x, 0.005f, -2.0f, 2.0f, "%.2f")) {
                        if (ImGui::IsItemActivated()) history.commitBefore(root);
                    }
                }
                ImGui::TextDisabled(tr("  Rejilla 2x2x2 sobre la caja del primer hijo; cada esquina se desplaza una fracción del tamaño."));
            } else {
                const int dk2 = node.deform.kind;
                if (dk2 != 9 && dk2 != 10 && dk2 != 12) {
                    propLabel(tr("Eje"));
                    const char* axes[] = {"X", "Y", "Z"};
                    int dax = node.deform.axis;
                    ImGui::SetNextItemWidth(-1);
                    if (ImGui::Combo("##defaxis", &dax, axes, 3)) {
                        history.commitBefore(root);
                        node.deform.axis = dax;
                    }
                }
                propLabel(node.deform.kind <= 1 ? tr("Ángulo") : dk2 == 10 ? tr("Mezcla") : dk2 == 12 ? tr("Tamaño") : tr("Fuerza"));
                float am = node.deform.amount;
                ImGui::SetNextItemWidth(-1);
                if (ImGui::DragFloat("##defamt", &am, 0.5f, -360.0f, 360.0f, node.deform.kind <= 1 ? "%.1f °" : "%.1f %%")) {
                    if (ImGui::IsItemActivated()) history.commitBefore(root);
                    node.deform.amount = am;
                }
                ImGui::TextDisabled(dk2 == 10 ? tr("  Mezcla el primer hijo con el segundo (mismo número de puntos).")
                                              : tr("  Deforma al primer hijo con malla; el hijo se edita aparte."));
            }
        } else if (node.env.present) {
            sectionHeader(tr("Propiedades del Objeto"));
            propLabel(tr("Color Superior"));
            ImGui::SetNextItemWidth(-1);
            if (ImGui::ColorEdit3("##envtop", &node.env.top.x, ImGuiColorEditFlags_DisplayHSV | ImGuiColorEditFlags_PickerHueBar)) {
                if (ImGui::IsItemActivated()) history.commitBefore(root);
            }
            propLabel(tr("Color Inferior"));
            ImGui::SetNextItemWidth(-1);
            if (ImGui::ColorEdit3("##envbot", &node.env.bottom.x, ImGuiColorEditFlags_DisplayHSV | ImGuiColorEditFlags_PickerHueBar)) {
                if (ImGui::IsItemActivated()) history.commitBefore(root);
            }
            ImGui::TextDisabled(tr("  Es el fondo del render en el Visor de Imágenes (degradado de arriba abajo)."));
        } else if (node.instance.present) {
            sectionHeader(tr("Propiedades del Objeto"));
            propLabel(tr("Objeto de Origen"));
            char ibuf[256];
            std::snprintf(ibuf, sizeof ibuf, "%s", node.instance.source.c_str());
            ImGui::SetNextItemWidth(-1);
            if (ImGui::InputText("##instsrc", ibuf, sizeof ibuf)) {
                if (ImGui::IsItemActivated()) history.commitBefore(root);
                node.instance.source = ibuf;
            }
            ImGui::TextDisabled(findNodeByPath(root, node.instance.source) ? tr("  Comparte la malla del objeto de origen.")
                                                                             : tr("  No se encuentra el objeto de origen (ruta por nombres)."));
        } else if (node.clone.present) {
            sectionHeader(tr("Propiedades del Objeto"));
            CloneData& cl = node.clone;
            propLabel(tr("Modo"));
            const char* modes[] = {tr("Lineal"), tr("Cuadrícula"), tr("Radial"), tr("Objeto")};
            ImGui::SetNextItemWidth(-1);
            int cmode = cl.mode;
            if (ImGui::Combo("##clmode", &cmode, modes, 4)) {
                history.commitBefore(root);
                cl.mode = cmode;
            }
            auto intRow = [&](const char* label, const char* id, int& val, int lo, int hi) {
                propLabel(label);
                ImGui::SetNextItemWidth(-1);
                int v = val;
                if (ImGui::DragInt(id, &v, 0.2f, lo, hi)) {
                    if (ImGui::IsItemActivated()) history.commitBefore(root);
                    val = std::clamp(v, lo, hi);
                }
            };
            auto fRow = [&](const char* label, const char* id, float& val, float speed, const char* fmt) {
                propLabel(label);
                ImGui::SetNextItemWidth(-1);
                float v = val;
                if (ImGui::DragFloat(id, &v, speed, -100000.0f, 100000.0f, fmt)) {
                    if (ImGui::IsItemActivated()) history.commitBefore(root);
                    val = v;
                }
            };
            if (cl.mode != 3) intRow(cl.mode == 1 ? tr("Cantidad X") : tr("Cantidad"), "##clcount", cl.count, 1, 1000);
            if (cl.mode == 1) {
                intRow(tr("Cantidad Y"), "##clcy", cl.countY, 1, 1000);
                intRow(tr("Cantidad Z"), "##clcz", cl.countZ, 1, 1000);
            }
            if (cl.mode == 2) {
                fRow(tr("Radio"), "##clr", cl.radiusCm, 1.0f, "%.1f cm");
                propLabel(tr("Eje"));
                const char* axes[] = {"X", "Y", "Z"};
                ImGui::SetNextItemWidth(-1);
                int cax = cl.axis;
                if (ImGui::Combo("##claxis", &cax, axes, 3)) {
                    history.commitBefore(root);
                    cl.axis = cax;
                }
            } else if (cl.mode != 3) {
                fRow(cl.mode == 1 ? tr("Separación X") : tr("Paso X"), "##clox", cl.offset.x, 1.0f, "%.1f cm");
                fRow(cl.mode == 1 ? tr("Separación Y") : tr("Paso Y"), "##cloy", cl.offset.y, 1.0f, "%.1f cm");
                fRow(cl.mode == 1 ? tr("Separación Z") : tr("Paso Z"), "##cloz", cl.offset.z, 1.0f, "%.1f cm");
            }
            fRow(tr("Escala por copia"), "##clsc", cl.scaleStepPct, 0.5f, "%.1f %%");
            fRow(tr("Giro por copia"), "##clrot", cl.rotStepDeg, 0.5f, "%.1f °");
            sectionHeader(tr("Efector Aleatorio"));
            fRow(tr("Posición Aleatoria X"), "##clrpx", cl.randPosCm.x, 1.0f, "%.1f cm");
            fRow(tr("Posición Aleatoria Y"), "##clrpy", cl.randPosCm.y, 1.0f, "%.1f cm");
            fRow(tr("Posición Aleatoria Z"), "##clrpz", cl.randPosCm.z, 1.0f, "%.1f cm");
            fRow(tr("Giro Aleatorio"), "##clrrot", cl.randRotDeg, 0.5f, "%.1f °");
            fRow(tr("Escala Aleatoria"), "##clrsc", cl.randScalePct, 0.5f, "%.1f %%");
            intRow(tr("Semilla"), "##clseed", cl.seed, 0, 100000);
            ImGui::TextDisabled(tr("  Clona el primer hijo con malla; el hijo se edita aparte."));
            if (cl.mode == 3) ImGui::TextDisabled(tr("  Modo Objeto: una copia en cada punto del 2.º hijo con malla."));
            else ImGui::TextDisabled((std::to_string(cloneCount(cl)) + " " + std::string(tr("copias"))).c_str());
        } else if (node.symmetry.present) {
            sectionHeader(tr("Propiedades del Objeto"));
            propLabel(tr("Plano de Simetría"));
            int ax = node.symmetry.axis;
            const char* planes[] = {tr("ZY (refleja X)"), tr("XZ (refleja Y)"), tr("XY (refleja Z)")};
            ImGui::SetNextItemWidth(-1);
            if (ImGui::Combo("##symplane", &ax, planes, 3)) {
                history.commitBefore(root);
                node.symmetry.axis = ax;
            }
            propLabel(tr("Tolerancia"));
            float tol = node.symmetry.tolCm;
            ImGui::SetNextItemWidth(-1);
            if (ImGui::DragFloat("##symtol", &tol, 0.05f, 0.0f, 50.0f, tr("%.2f cm"))) {
                if (ImGui::IsItemActivated()) history.commitBefore(root);
                node.symmetry.tolCm = tol;
            }
            ImGui::TextDisabled(tr("  Refleja al primer hijo con malla; el hijo se edita aparte."));
        } else if (isNull) {
            sectionHeader(tr("Propiedades del Objeto"));
            roCombo(tr("Mostrar"), tr("Punto"));
            roFloat(tr("Radio"), 10.0f, tr("%.0f cm"));
            roFloat(tr("Relación de Aspecto"), 1.0f, "%.2f");
            roCombo(tr("Orientación"), tr("Cámara"));
        } else {
            sectionHeader(tr("Propiedades del Objeto"));
            if (node.primitive == PrimitiveType::Cube) {
                roFloat(tr("Tamaño . X"), 200, tr("%.0f cm"));
                roFloat(tr("Tamaño . Y"), 200, tr("%.0f cm"));
                roFloat(tr("Tamaño . Z"), 200, tr("%.0f cm"));
                roFloat(tr("Segmentos en X"), 1, "%.0f");
                roFloat(tr("Segmentos en Y"), 1, "%.0f");
                roFloat(tr("Segmentos en Z"), 1, "%.0f");
                roCheck(tr("Superficies Separadas"), false);
                roCheck(tr("Canto"), false);
            } else if (node.primitive == PrimitiveType::Mesh && node.param.kind != 0 && node.mesh == node.param.generated) {
                ParamShape& ps = node.param;
                struct Field { const char* label; float* v; float mn, mx; };
                std::vector<Field> fl;
                std::vector<std::pair<const char*, int*>> il;
                switch (ps.kind) {
                    case 1: case 2: fl = {{"Radio", &ps.a, 0.1f, 5000}, {"Altura", &ps.b, 0.1f, 5000}}; il = {{"Segmentos de Rotación", &ps.seg1}}; break;
                    case 3: fl = {{"Radio Exterior", &ps.a, 0.1f, 5000}, {"Radio Interior", &ps.b, 0.0f, 5000}}; il = {{"Segmentos de Rotación", &ps.seg1}}; break;
                    case 4: fl = {{"Radio del Anillo", &ps.a, 0.1f, 5000}, {"Radio del Tubo", &ps.b, 0.1f, 5000}}; il = {{"Segmentos del Anillo", &ps.seg1}, {"Segmentos del Tubo", &ps.seg2}}; break;
                    case 5: fl = {{"Radio", &ps.a, 0.1f, 5000}, {"Altura", &ps.b, 0.2f, 5000}}; il = {{"Segmentos de Rotación", &ps.seg1}, {"Segmentos de la Tapa", &ps.seg2}}; break;
                    case 6: fl = {{"Radio Interior", &ps.a, 0.1f, 5000}, {"Radio Exterior", &ps.b, 0.2f, 5000}, {"Altura", &ps.c, 0.1f, 5000}}; il = {{"Segmentos de Rotación", &ps.seg1}}; break;
                    case 7: fl = {{"Anchura", &ps.a, 0.1f, 5000}, {"Profundidad", &ps.b, 0.1f, 5000}, {"Altura", &ps.c, 0.1f, 5000}}; break;
                    case 8: fl = {{"Radio", &ps.a, 0.1f, 5000}}; break;
                    default: break;
                }
                bool changed = false;
                int id = 0;
                for (Field& f : fl) {
                    propLabel(tr(f.label));
                    ImGui::SetNextItemWidth(fieldWidth());
                    ImGui::PushID(id++);
                    if (ImGui::DragFloat("##pf", f.v, 0.5f, f.mn, f.mx, tr("%.1f cm"))) { *f.v = std::clamp(*f.v, f.mn, f.mx); changed = true; }
                    trackEdit(history, root);
                    ImGui::PopID();
                }
                for (auto& f : il) {
                    propLabel(tr(f.first));
                    ImGui::SetNextItemWidth(fieldWidth());
                    ImGui::PushID(id++);
                    if (ImGui::DragInt("##pi", f.second, 0.2f, 3, 128)) { *f.second = std::clamp(*f.second, 3, 128); changed = true; }
                    trackEdit(history, root);
                    ImGui::PopID();
                }
                if (ps.kind == 8) {
                    propLabel(tr("Tipo"));
                    const char* kinds[] = {tr("Tetraedro"), tr("Octaedro"), tr("Icosaedro"), tr("Dodecaedro")};
                    ImGui::SetNextItemWidth(fieldWidth());
                    if (ImGui::Combo("##pk", &ps.seg1, kinds, 4)) { history.commitBefore(root); changed = true; }
                }
                if (changed) {
                    MeshData nm = buildParamShape(ps);
                    node.mesh = std::make_shared<const MeshData>(std::move(nm));
                    ps.generated = node.mesh;
                    g.comp.sel.clear();
                }
                ImGui::TextDisabled(tr("  Primitiva paramétrica: al editar la malla (puntos, aristas...) pierde los parámetros."));
            } else if (node.primitive == PrimitiveType::Mesh) {
                const MeshData* m = node.mesh.get();
                roFloat(tr("Vértices"), m ? (float)m->vertices.size() : 0.0f, "%.0f");
                roFloat(tr("Triángulos"), m ? (float)(m->indices.size() / 3) : 0.0f, "%.0f");
                ImGui::TextDisabled(tr("  Objeto Poligonal (importado o hecho editable con C)."));
                ImGui::TextDisabled(tr("  Puntos, aristas y polígonos: botones de la barra izquierda (Supr borra)."));
            } else if (node.primitive == PrimitiveType::Sphere) {
                roFloat(tr("Radio"), 100, tr("%.0f cm"));
                roFloat(tr("Segmentos"), 24, "%.0f");
                roCombo(tr("Tipo"), tr("Estándar"));
                roCheck(tr("Perfectos de Render"), true);
            } else {
                roFloat(tr("Anchura"), 200, tr("%.0f cm"));
                roFloat(tr("Altura"), 200, tr("%.0f cm"));
                roFloat(tr("Segmentos en la Anchura"), 1, "%.0f");
                roFloat(tr("Segmentos en la Altura"), 1, "%.0f");
                roCombo(tr("Orientación"), "+Y");
            }
            ImGui::TextDisabled(tr("  Para cambiar el tamaño use Escala (Coord.) o el Gestor de Coordenadas."));
        }
    }
    if (!isNull && !isLight && tabOn(mask, 3)) {
        sectionHeader(tr("Etiqueta Phong"));
        roCheck(tr("Limitar Ángulo"), true);
        roFloat(tr("Ángulo de Phong"), 80.0f, "%.0f °");
        roCheck(tr("Usar Bordes"), false);
    }
    if (isLight && tabOn(mask, 3)) {
        LightData& l = node.light;
        sectionHeader(tr("Detalles"));
        const bool spot = l.kind == LightKind::Spot || l.kind == LightKind::SpotSquare ||
                          l.kind == LightKind::ParallelSpot || l.kind == LightKind::ParallelSpotSquare;
        ImGui::BeginDisabled(!spot);
        propLabel(tr("Ángulo Interior"));
        ImGui::SetNextItemWidth(fieldWidth());
        if (ImGui::DragFloat("##linner", &l.innerAngleDeg, 0.5f, 0.0f, 175.0f, "%.1f °"))
            l.innerAngleDeg = std::clamp(l.innerAngleDeg, 0.0f, l.outerAngleDeg);
        trackEdit(history, root);
        propLabel(tr("Ángulo Exterior"));
        ImGui::SetNextItemWidth(fieldWidth());
        if (ImGui::DragFloat("##louter", &l.outerAngleDeg, 0.5f, 0.1f, 175.0f, "%.1f °")) {
            l.outerAngleDeg = std::clamp(l.outerAngleDeg, 0.1f, 175.0f);
            l.innerAngleDeg = std::min(l.innerAngleDeg, l.outerAngleDeg);
        }
        trackEdit(history, root);
        ImGui::EndDisabled();
        // Mismo orden que el desplegable de C4D; el valor guardado es el de
        // LIGHT_DETAILS_FALLOFF.
        static const char* falloffNames[] = {tr("Ninguno"), tr("Inverso Cuadrado (Físicamente Exacto)"), tr("Lineal"), tr("Escalón"),
                                             tr("Inverso Cuadrado Limitado"), tr("Inverso Limitado"), tr("Inverso")};
        static const int falloffIds[] = {0, 10, 8, 5, 7, 6, 9};
        int fi = 0;
        for (int i = 0; i < IM_ARRAYSIZE(falloffIds); ++i)
            if (falloffIds[i] == l.falloff) fi = i;
        propLabel(tr("Decaimiento"), false);
        ImGui::SetNextItemWidth(fieldWidth(240));
        if (ImGui::Combo("##lfalloff", &fi, falloffNames, IM_ARRAYSIZE(falloffNames))) {
            history.commitBefore(root);
            l.falloff = falloffIds[fi];
        }
        setHint(tr("Cómo pierde fuerza la luz con la distancia. En las inversas, el Radio es la distancia a la que da el 100 %."));
        ImGui::BeginDisabled(l.falloff == 0);
        propLabel(tr("Radio Interior"));
        float innerCm = l.innerRadius * kCmPerUnit;
        ImGui::SetNextItemWidth(fieldWidth());
        if (ImGui::DragFloat("##lir", &innerCm, 1.0f, 0.0f, 1e7f, tr("%.0f cm"))) l.innerRadius = std::max(innerCm, 0.0f) / kCmPerUnit;
        trackEdit(history, root);
        propLabel(tr("Radio/Decaimiento"));
        float radCm = l.falloffRadius * kCmPerUnit;
        ImGui::SetNextItemWidth(fieldWidth());
        if (ImGui::DragFloat("##lr", &radCm, 1.0f, 0.1f, 1e7f, tr("%.0f cm"))) l.falloffRadius = std::max(radCm, 0.1f) / kCmPerUnit;
        trackEdit(history, root);
        ImGui::EndDisabled();
        if (l.kind == LightKind::Area) {
            sectionHeader(tr("Área"));
            const char* axes[] = {tr("Tamaño Horizontal"), tr("Tamaño Vertical"), tr("Tamaño en Profundidad")};
            for (int i = 0; i < 3; ++i) {
                ImGui::PushID(i);
                propLabel(axes[i]);
                float cm = l.areaSize[i] * kCmPerUnit;
                ImGui::SetNextItemWidth(fieldWidth());
                if (ImGui::DragFloat("##as", &cm, 1.0f, 0.1f, 1e7f, tr("%.0f cm"))) l.areaSize[i] = std::max(cm, 0.1f) / kCmPerUnit;
                trackEdit(history, root);
                ImGui::PopID();
            }
        }
    }
    if (isLight && tabOn(mask, 5)) {
        LightData& l = node.light;
        sectionHeader(tr("Sombra"));
        static const char* shadows[] = {tr("Ninguno"), tr("Mapeado (Suave)"), tr("Trazado (Duro)"), tr("Área")};
        propLabel(tr("Sombra"), false);
        int sh = std::clamp(l.shadow, 0, 3);
        ImGui::SetNextItemWidth(fieldWidth());
        if (ImGui::Combo("##lshadow2", &sh, shadows, IM_ARRAYSIZE(shadows))) {
            history.commitBefore(root);
            l.shadow = sh;
        }
        ImGui::TextDisabled(tr("  En el visor, las sombras son siempre mapeadas y de una sola luz."));
    }
    if (isLight && (mask & ~0x2Fu)) {
        sectionHeader(tr("Página no disponible"));
        ImGui::TextDisabled(tr("  Todavía no implementado en Iron3D."));
    }
}

// --- Ajustes de material como el editor de R19 (2026-11-11) -----------------------

const char* kTexMixNames[] = {tr("Normal"), tr("Añadir"), tr("Restar"), tr("Multiplicar")};
const char* kSamplingNames[] = {tr("Ninguno"), tr("Circular"), tr("Cuadrado"), tr("Alias 1"), tr("Alias 2"), tr("Alias 3"), tr("MIP"), tr("SAT")};
// REFLECTION_DISTRIBUTION_* en el orden de sus valores.
const char* kReflDistNames[] = {tr("Especular - Blinn (Heredado)"), tr("Especular - Phong (Heredado)"), tr("Beckmann"), tr("GGX"),
                                tr("Phong"), tr("Ward"), tr("Lambertiano (Difuso)"), tr("Oren-Nayar (Difuso)"), tr("Irawan (Tejido)")};
// REFLECTION_ADDITIVE_MODE_* en el orden de sus valores.
const char* kReflAttNames[] = {tr("Promedio"), tr("Mínimo"), tr("Máximo"), tr("Añadir"), tr("Metal")};
const char* kReflFresnelNames[] = {tr("Ninguno"), tr("Dieléctrico"), tr("Conductor")};

// Porcentaje que solo se aplica al soltar el control (los que cambian la imagen
// horneada del canal: arrastrar no crea una textura nueva en cada frame).
bool pctDeferred(const char* label, const char* id, float& value, float maxPct, SceneHistory& history, SceneNode& root) {
    static ImGuiID activeId = 0;
    static float draft = 0.0f;
    propLabel(label);
    ImGui::SetNextItemWidth(fieldWidth());
    const ImGuiID me = ImGui::GetID(id);
    float shown = activeId == me ? draft : value * 100.0f;
    ImGui::DragFloat(id, &shown, 0.5f, -maxPct, maxPct, "%.0f %%");
    bool applied = false;
    if (ImGui::IsItemActive()) {
        activeId = me;
        draft = shown;
    }
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        value = draft / 100.0f;
        applied = true;
    }
    if (!ImGui::IsItemActive() && activeId == me) activeId = 0;
    trackEdit(history, root);
    return applied;
}

// Porcentaje normal (no toca la textura horneada).
bool pctField(const char* label, const char* id, float& value, float minPct, float maxPct, SceneHistory& history,
              SceneNode& root) {
    propLabel(label);
    ImGui::SetNextItemWidth(fieldWidth());
    float pct = value * 100.0f;
    const bool ch = ImGui::DragFloat(id, &pct, 0.5f, minPct, maxPct, "%.0f %%");
    if (ch) value = std::clamp(pct, minPct, maxPct) / 100.0f;
    trackEdit(history, root);
    return ch;
}

bool comboField(const char* label, const char* id, int& value, const char* const* names, int count, SceneHistory& history,
                SceneNode& root, float width = 200.0f) {
    propLabel(label, false);
    ImGui::SetNextItemWidth(fieldWidth(width));
    int v = std::clamp(value, 0, count - 1);
    if (ImGui::Combo(id, &v, names, count)) {
        history.commitBefore(root);
        value = v;
        return true;
    }
    return false;
}

bool* channelFlag(Material& m, TexSlot s) {
    switch (s) {
        case TexSlot::Color: return &m.useColor;
        case TexSlot::Emission: return &m.useLuminance;
        case TexSlot::Transparency: return &m.useTransparency;
        case TexSlot::Normal: return &m.useNormal;
        case TexSlot::Bump: return &m.useBump;
        case TexSlot::Alpha: return &m.useAlpha;
        default: return nullptr;
    }
}

// Campos derivados (lo que pinta el visor) a partir de los ajustes del canal.
void syncMaterialChannels(Material& m) {
    for (int i = 0; i < kTexSlotCount; ++i) {
        const TexSlot s = (TexSlot)i;
        bool* on = channelFlag(m, s);
        if (!on) continue;
        ChannelSettings& c = m.channel(s);
        if (!*on) {
            // Se guarda la ruta en el arbol; el canal apagado no pinta textura.
            if (!m.texture(s).empty() && c.shader.empty()) {
                c.shader.kind = ShaderKind::Bitmap;
                c.shader.path = m.texture(s);
            }
            m.texture(s).clear();
        } else if (m.texture(s).empty()) {
            if (const ShaderNode* b = c.shader.firstBitmap()) m.texture(s) = b->path;
        }
    }
    const ChannelSettings& col = m.channel(TexSlot::Color);
    if (m.metallic < 0.5f) m.baseColor = m.useColor ? glm::clamp(col.color * col.brightness, 0.0f, 1.0f) : glm::vec3(0.0f);
    const ChannelSettings& lum = m.channel(TexSlot::Emission);
    m.emission = m.useLuminance ? lum.color * lum.brightness : glm::vec3(0.0f);
    const ChannelSettings& tr = m.channel(TexSlot::Transparency);
    m.transparency = m.useTransparency ? tr.color * tr.brightness : glm::vec3(0.0f);
}

std::string leafName(const std::string& p) {
    const size_t s = p.find_last_of("/\\");
    return s == std::string::npos ? p : p.substr(s + 1);
}

void shaderTreeLines(const ShaderNode& n, int depth) {
    char line[400];
    std::string extra;
    switch (n.kind) {
        case ShaderKind::Bitmap: extra = leafName(n.path); break;
        case ShaderKind::Filter: {
            char b[200];
            std::snprintf(b, sizeof(b), tr("Tono %.0f°  Sat %.0f%%  Lum %.0f%%  Brillo %.0f%%  Contraste %.0f%%  Gamma %.2f"), n.hue,
                          n.saturation * 100, n.lightness * 100, n.fBrightness * 100, n.contrast * 100, n.gamma);
            extra = b;
            break;
        }
        case ShaderKind::Colorizer:
        case ShaderKind::Gradient: extra = std::to_string(n.gradient.size()) + tr(" nudos"); break;
        case ShaderKind::Layer: extra = std::to_string(n.layers.size()) + tr(" capas"); break;
        case ShaderKind::Other: extra = tr("no se evalúa en el visor"); break;
        default: break;
    }
    std::snprintf(line, sizeof(line), "%*s%s \"%s\"%s%s", depth * 3, "", shaderKindName(n.kind), n.name.c_str(),
                  extra.empty() ? "" : "  —  ", extra.c_str());
    ImGui::TextDisabled("  %s", line);
    for (const ShaderNode& c : n.children) shaderTreeLines(c, depth + 1);
}

// Campo Textura: shader + imagen, "..." para elegir otra, "x" para quitarla.
void textureField(Material& m, TexSlot s, SceneHistory& history, SceneNode& root) {
    ChannelSettings& c = m.channel(s);
    const ShaderNode* eff = effectiveShader(m, s);
    std::string shown;
    if (eff && !eff->isPlainBitmap()) shown = std::string(shaderKindName(eff->kind)) + " (" + leafName(m.texture(s)) + ")";
    else shown = leafName(m.texture(s));
    propLabel(tr("Textura"), false);
    ImGui::SetNextItemWidth(std::max(60.0f, fieldWidth(240) - 56.0f));
    char buf[260];
    std::snprintf(buf, sizeof(buf), "%s", shown.c_str());
    ImGui::InputText("##tex", buf, sizeof(buf), ImGuiInputTextFlags_ReadOnly);
    if (ImGui::IsItemHovered() && !m.texture(s).empty()) ImGui::SetTooltip("%s", m.texture(s).c_str());
    ImGui::SameLine(0, 2);
    if (ImGui::Button(tr("...##texpick")) && nativeFileDialogAvailable()) {
        if (auto p = openFileDialog(tr("Elegir imagen"), {{tr("Imágenes"), "*.png;*.jpg;*.jpeg;*.bmp;*.tga;*.psd"}})) {
            history.commitBefore(root);
            const std::u8string u = p->u8string();
            m.texture(s) = std::string(u.begin(), u.end());
            c.shader = {}; // imagen nueva sola
            if (bool* on = channelFlag(m, s)) *on = true;
        }
    }
    setHint(tr("Elegir la imagen del canal (sustituye al shader importado)."));
    ImGui::SameLine(0, 2);
    if (ImGui::Button(tr("x##texclear"))) {
        history.commitBefore(root);
        m.texture(s).clear();
        c.shader = {};
    }
    setHint(tr("Quitar la textura del canal."));
    if (eff && !eff->isPlainBitmap()) {
        shaderTreeLines(*eff, 0);
        if (eff->hasUnsupported()) ImGui::TextColored(ImVec4(0.95f, 0.7f, 0.3f, 1), tr("  Hay shaders que el visor todavía no evalúa."));
    }
}

// Bloque de un canal con color (Color, Luminancia, Transparencia).
void colorChannelBlock(Material& m, TexSlot s, SceneHistory& history, SceneNode& root) {
    ChannelSettings& c = m.channel(s);
    ImGui::PushID((int)s);
    propLabel(tr("Color"));
    ImGui::SetNextItemWidth(fieldWidth(240));
    if (ImGui::ColorEdit3("##ccol", &c.color.x, ImGuiColorEditFlags_DisplayHSV | ImGuiColorEditFlags_PickerHueBar))
        syncMaterialChannels(m);
    trackEdit(history, root);
    if (pctDeferred(tr("Brillo"), "##cbr", c.brightness, 1000.0f, history, root)) syncMaterialChannels(m);
    textureField(m, s, history, root);
    comboField(tr("Muestreo"), "##csamp", c.sampling, kSamplingNames, IM_ARRAYSIZE(kSamplingNames), history, root, 140);
    pctField(tr("Desenfoque Desfase"), "##cboff", c.blurOffset, -100.0f, 100.0f, history, root);
    pctField(tr("Desenfoque Escala"), "##cbsc", c.blurScale, -100.0f, 100.0f, history, root);
    comboField(tr("Modo de Mezcla"), "##cmix", c.mixMode, kTexMixNames, IM_ARRAYSIZE(kTexMixNames), history, root, 140);
    pctDeferred(tr("Fuerza de Mezcla"), "##cstr", c.mixStrength, 100.0f, history, root);
    ImGui::PopID();
}

// Canal sin color (Relieve, Normal, Alfa).
void plainChannelBlock(Material& m, TexSlot s, SceneHistory& history, SceneNode& root) {
    ImGui::PushID((int)s);
    if (s == TexSlot::Bump) pctField(tr("Fuerza"), "##bstr", m.bumpStrength, -1000.0f, 1000.0f, history, root);
    if (s == TexSlot::Normal) pctField(tr("Fuerza"), "##nstr", m.normalStrength, -1000.0f, 1000.0f, history, root);
    if (s == TexSlot::Alpha) {
        propLabel(tr("Color"));
        ImGui::SetNextItemWidth(fieldWidth(240));
        ImGui::ColorEdit3("##acol", &m.alphaColor.x, ImGuiColorEditFlags_DisplayHSV);
        trackEdit(history, root);
        pctField(tr("Delta"), "##adelta", m.alphaDelta, 0.0f, 100.0f, history, root);
        propLabel(tr("Invertir"), false);
        bool b = m.alphaInvert;
        if (ImGui::Checkbox("##ainv", &b)) { history.commitBefore(root); m.alphaInvert = b; }
        propLabel(tr("Alfa Suave"), false);
        b = m.alphaSoft;
        if (ImGui::Checkbox("##asoft", &b)) { history.commitBefore(root); m.alphaSoft = b; }
        propLabel(tr("Alfa de Imagen"), false);
        b = m.alphaImage;
        if (ImGui::Checkbox("##aimg", &b)) { history.commitBefore(root); m.alphaImage = b; }
    }
    textureField(m, s, history, root);
    comboField(tr("Muestreo"), "##psamp", m.channel(s).sampling, kSamplingNames, IM_ARRAYSIZE(kSamplingNames), history, root, 140);
    ImGui::PopID();
}

void colorTabExtras(Material& m, SceneHistory& history, SceneNode& root) {
    static const char* models[] = {tr("Lambertiano"), tr("Oren-Nayar")};
    comboField(tr("Modelo"), "##dmodel", m.diffuseModel, models, IM_ARRAYSIZE(models), history, root, 140);
    pctField(tr("Caída Difusa"), "##dfall", m.diffuseFalloff, -100.0f, 100.0f, history, root);
    pctField(tr("Nivel Difuso"), "##dlevel", m.diffuseLevel, 0.0f, 1000.0f, history, root);
    ImGui::BeginDisabled(m.diffuseModel != 1);
    pctField(tr("Rugosidad"), "##drough", m.diffuseRoughness, 0.0f, 100.0f, history, root);
    ImGui::EndDisabled();
}

void reflectancePanel(Material& m, SceneHistory& history, SceneNode& root) {
    sectionHeader(tr("Global"));
    pctField(tr("Brillo Reflexión Global"), "##grefl", m.reflectionBrightness, 0.0f, 1000.0f, history, root);
    pctField(tr("Brillo Especular Global"), "##gspec", m.specularBrightness, 0.0f, 1000.0f, history, root);
    propLabel(tr("Pase Separado"), false);
    bool sep = m.separatePass;
    if (ImGui::Checkbox("##gsep", &sep)) { history.commitBefore(root); m.separatePass = sep; }
    bool layersChanged = false;
    for (size_t i = 0; i < m.layers.size(); ++i) {
        ReflectLayer& l = m.layers[i];
        ImGui::PushID((int)i);
        char head[200];
        std::snprintf(head, sizeof(head), "%s  (%s)###lay", l.name.empty() ? tr("Capa") : l.name.c_str(),
                      l.blendMode == 1 ? tr("Añadir") : tr("Normal"));
        if (ImGui::CollapsingHeader(head, ImGuiTreeNodeFlags_DefaultOpen)) {
            int dist = std::clamp(l.distribution, 0, IM_ARRAYSIZE(kReflDistNames) - 1);
            if (comboField(tr("Tipo"), "##ldist", dist, kReflDistNames, IM_ARRAYSIZE(kReflDistNames), history, root, 220)) {
                l.distribution = dist;
                layersChanged = true;
            }
            comboField(tr("Atenuación"), "##latt", l.attenuation, kReflAttNames, IM_ARRAYSIZE(kReflAttNames), history, root, 140);
            layersChanged |= pctField(tr("Rugosidad"), "##lrough", l.roughness, 0.0f, 100.0f, history, root);
            layersChanged |= pctField(tr("Fuerza Reflexión"), "##lrefl", l.strength, 0.0f, 1000.0f, history, root);
            layersChanged |= pctField(tr("Fuerza Especular"), "##lspec", l.specular, 0.0f, 1000.0f, history, root);
            pctField(tr("Fuerza Relieve"), "##lbump", l.bumpStrength, -1000.0f, 1000.0f, history, root);
            sectionHeader(tr("Color de Capa"));
            propLabel(tr("Color"));
            ImGui::SetNextItemWidth(fieldWidth(240));
            if (ImGui::ColorEdit3("##lcol", &l.colorRaw.x, ImGuiColorEditFlags_DisplayHSV)) {
                l.color = l.colorRaw * l.colorBrightness;
                layersChanged = true;
            }
            trackEdit(history, root);
            if (pctField(tr("Brillo"), "##lcbr", l.colorBrightness, 0.0f, 1000.0f, history, root)) {
                l.color = l.colorRaw * l.colorBrightness;
                layersChanged = true;
            }
            propLabel(tr("Textura"), false);
            ImGui::TextDisabled("%s", l.colorTex.empty() ? tr("(ninguna)") : leafName(l.colorTex).c_str());
            comboField(tr("Modo de Mezcla"), "##lcmix", l.colorMixMode, kTexMixNames, IM_ARRAYSIZE(kTexMixNames), history, root, 140);
            pctField(tr("Fuerza de Mezcla"), "##lcstr", l.colorMixStrength, 0.0f, 100.0f, history, root);
            sectionHeader(tr("Fresnel de Capa"));
            if (comboField(tr("Fresnel"), "##lfres", l.fresnel, kReflFresnelNames, IM_ARRAYSIZE(kReflFresnelNames), history, root, 140))
                layersChanged = true;
            ImGui::BeginDisabled(l.fresnel == 0);
            propLabel(tr("IOR"));
            ImGui::SetNextItemWidth(fieldWidth());
            if (ImGui::DragFloat("##lior", &l.ior, 0.01f, 1.0f, 10.0f, "%.3f")) layersChanged = true;
            trackEdit(history, root);
            ImGui::EndDisabled();
        }
        ImGui::PopID();
    }
    if (m.layers.empty()) ImGui::TextDisabled(tr("  Sin capas importadas: el visor usa Metálico/Rugosidad."));
    if (layersChanged) {
        glm::vec3 c = m.baseColor;
        float metal = m.metallic, rough = m.roughness;
        if (pbrFromLayers(m.layers, m.useColor, c, metal, rough)) {
            m.baseColor = c;
            m.metallic = metal;
            m.roughness = rough;
        }
    }
    sectionHeader(tr("Visor (PBR equivalente)"));
    propLabel(tr("Metálico"));
    float metallicPct = m.metallic * 100.0f;
    ImGui::SetNextItemWidth(fieldWidth());
    if (ImGui::SliderFloat("##metallic", &metallicPct, 0.0f, 100.0f, "%.0f %%")) m.metallic = metallicPct / 100.0f;
    trackEdit(history, root);
    propLabel(tr("Rugosidad"));
    float roughnessPct = m.roughness * 100.0f;
    ImGui::SetNextItemWidth(fieldWidth());
    if (ImGui::SliderFloat("##roughness", &roughnessPct, 0.0f, 100.0f, "%.0f %%")) m.roughness = roughnessPct / 100.0f;
    trackEdit(history, root);
    ImGui::TextDisabled(tr("  El visor todavía no pinta cada capa por separado (brillos de color, Atenuación)."));
}

void channelChecks(Material& m, SceneHistory& history, SceneNode& root) {
    struct Row { Tr name; bool* flag; };
    const Row rows[] = {{tr("Color"), &m.useColor},          {tr("Difusión"), nullptr},          {tr("Luminancia"), &m.useLuminance},
                        {"Transparencia", &m.useTransparency}, {tr("Reflectancia"), &m.useReflectance}, {tr("Entorno"), nullptr},
                        {"Niebla", nullptr},          {tr("Relieve"), &m.useBump},        {tr("Normal"), &m.useNormal},
                        {"Alfa", &m.useAlpha},        {tr("Fosforescencia"), nullptr},    {tr("Desplazamiento"), nullptr}};
    for (const Row& r : rows) {
        if (!r.flag) {
            roCheck(r.name, false, false);
            continue;
        }
        propLabel(r.name, false);
        bool v = *r.flag;
        ImGui::PushID(r.name);
        if (ImGui::Checkbox("##on", &v)) {
            history.commitBefore(root);
            *r.flag = v;
            syncMaterialChannels(m);
        }
        ImGui::PopID();
    }
}

void drawMaterialAttributes(Material& m, int index, SceneNode& root, SceneHistory& history) {
    char title[200];
    std::snprintf(title, sizeof(title), tr("Material [%s]"), m.name.c_str());
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
    static const char* tabs[] = {tr("Básico"), tr("Color"), tr("Luminancia"), tr("Transparencia"), tr("Reflectancia"), tr("Relieve"),
                                 tr("Normal"), tr("Alfa"), tr("Iluminación"), tr("Editor"), tr("Asignar")};
    attrTabRow(tabs, IM_ARRAYSIZE(tabs), g.matMask);

    if (tabOn(g.matMask, 0)) {
        sectionHeader(tr("Propiedades Básicas"));
        propLabel(tr("Nombre"), false);
        char nameBuf[128];
        std::snprintf(nameBuf, sizeof(nameBuf), "%s", m.name.c_str());
        ImGui::SetNextItemWidth(fieldWidth(220));
        if (ImGui::InputText("##mname", nameBuf, sizeof(nameBuf)) && nameBuf[0]) m.name = nameBuf;
        trackEdit(history, root);
        roCombo(tr("Capa"), "");
        channelChecks(m, history, root);
    }
    if (tabOn(g.matMask, 1)) {
        sectionHeader(tr("Color"));
        colorChannelBlock(m, TexSlot::Color, history, root);
        colorTabExtras(m, history, root);
    }
    if (tabOn(g.matMask, 2)) {
        sectionHeader(tr("Luminancia"));
        colorChannelBlock(m, TexSlot::Emission, history, root);
    }
    if (tabOn(g.matMask, 3)) {
        sectionHeader(tr("Transparencia"));
        colorChannelBlock(m, TexSlot::Transparency, history, root);
        propLabel(tr("Refracción"));
        ImGui::SetNextItemWidth(fieldWidth());
        ImGui::DragFloat("##tior", &m.ior, 0.01f, 0.25f, 4.0f, "%.3f");
        trackEdit(history, root);
    }
    if (tabOn(g.matMask, 4)) reflectancePanel(m, history, root);
    if (tabOn(g.matMask, 5)) {
        sectionHeader(tr("Relieve"));
        plainChannelBlock(m, TexSlot::Bump, history, root);
    }
    if (tabOn(g.matMask, 6)) {
        sectionHeader(tr("Normal"));
        plainChannelBlock(m, TexSlot::Normal, history, root);
    }
    if (tabOn(g.matMask, 7)) {
        sectionHeader(tr("Alfa"));
        plainChannelBlock(m, TexSlot::Alpha, history, root);
    }
    if (tabOn(g.matMask, 8) || tabOn(g.matMask, 9)) {
        sectionHeader(tabOn(g.matMask, 8) ? tr("Iluminación") : tr("Editor"));
        ImGui::TextDisabled(tr("  Todavía no implementado en Iron3D."));
    }
    if (tabOn(g.matMask, 10)) {
        sectionHeader(tr("Asignar"));
        std::vector<SceneNode*> users;
        collectUsers(root, index, users);
        if (users.empty()) ImGui::TextDisabled(tr("  Ningún objeto usa este material. Arrástrelo al Gestor de Objetos."));
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
    titleLine(Icon::Sphere, tr("Etiqueta de Textura [Textura]"));
    static const char* tabs[] = {tr("Básico"), tr("Etiqueta"), tr("Coordenadas")};
    attrTabRow(tabs, IM_ARRAYSIZE(tabs), g.tagMask);
    if (tabOn(g.tagMask, 0)) {
        sectionHeader(tr("Propiedades Básicas"));
        roCombo(tr("Nombre"), tr("Textura"));
    }
    if (tabOn(g.tagMask, 1)) {
        sectionHeader(tr("Propiedades de Etiqueta"));
        propLabel(tr("Material"), false);
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
        roCombo(tr("Selección"), "");
        roCombo(tr("Proyección"), tr("Mapeado UVW"));
        roCombo(tr("Lado"), tr("Ambos"));
        roCheck(tr("Mezclar Texturas"), false);
        roCheck(tr("Repetición"), true);
        roCheck(tr("Sin Costuras"), false);
        roCheck(tr("Usar UVW para Relieve"), true);
        roFloat(tr("Desfase U"), 0, "%.0f %%");
        roFloat(tr("Desfase V"), 0, "%.0f %%");
        roFloat(tr("Longitud U"), 100, "%.0f %%");
        roFloat(tr("Longitud V"), 100, "%.0f %%");
        roFloat(tr("Repeticiones U"), 1, "%.0f");
        roFloat(tr("Repeticiones V"), 1, "%.0f");
    }
    if (tabOn(g.tagMask, 2)) {
        sectionHeader(tr("Coordenadas"));
        ImGui::TextDisabled(tr("  Todavía no implementado en Iron3D."));
    }
}

void drawAttributesManager(SceneNode& root, SceneNode* selected, SceneHistory& history,
                           std::vector<Material>& materials, ImVec2 pos, ImVec2 size) {
    beginPanel("##attrmgr", pos, size, ImGuiWindowFlags_MenuBar);
    if (ImGui::BeginMenuBar()) {
        panelGrip(tr(names().attributes));
        if (ImGui::BeginMenu(tr("Modo"))) {
            if (ImGui::MenuItem(tr("Objeto"), nullptr, g.focus == AttrFocus::Object)) g.focus = AttrFocus::Object;
            if (ImGui::MenuItem(tr("Etiqueta"), nullptr, g.focus == AttrFocus::Tag, selected && selected->materialIndex >= 0))
                g.focus = AttrFocus::Tag;
            if (ImGui::MenuItem(tr("Material"), nullptr, g.focus == AttrFocus::Material, g.selectedMat >= 0))
                g.focus = AttrFocus::Material;
            ImGui::Separator();
            if (ImGui::MenuItem(tr("Proyecto"), tr("Ctrl+D"), g.focus == AttrFocus::Project)) g.focus = AttrFocus::Project;
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu(tr("Editar"))) {
            disabledItems({{tr("Copiar"), ""}, {tr("Pegar"), ""}, {nullptr, nullptr}, {tr("Seleccionar Todo"), ""},
                           {tr("Mostrar Subcanales"), ""}});
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu(tr("Datos de Usuario"))) {
            disabledItems({{tr("Añadir Datos de Usuario..."), ""}, {tr("Gestionar Datos de Usuario..."), ""}});
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

// Entradas comunes del menu de claves (clic derecho en la regla, menu
// Animacion y Editor de Curvas).
void drawKeyMenuItems() {
    using KR = UiState::KeyReq;
    const bool any = !g.keySel.empty();
    ImGui::TextDisabled(tr("%zu clave%s seleccionada%s"), g.keySel.size(), g.keySel.size() == 1 ? "" : "s",
                        g.keySel.size() == 1 ? "" : "s");
    ImGui::Separator();
    auto interp = [&](const char* label, KeyInterp ki) {
        if (ImGui::MenuItem(label, nullptr, false, any)) {
            g.keyReq = KR::Interp;
            g.keyReqInterp = ki;
        }
    };
    interp(tr("Interpolación Spline"), KeyInterp::Spline);
    interp(tr("Interpolación Lineal"), KeyInterp::Linear);
    interp(tr("Interpolación Escalón"), KeyInterp::Step);
    ImGui::Separator();
    if (ImGui::MenuItem(tr("Eliminar Claves"), tr("Supr"), false, any)) g.keyReq = KR::Delete;
}

void applyKeyRequest(SceneNode& root, SceneNode* selected, SceneHistory& history) {
    using KR = UiState::KeyReq;
    const KR req = g.keyReq;
    g.keyReq = KR::None;
    if (req == KR::None || !selected || g.keySel.empty()) return;
    const std::vector<int> frames(g.keySel.begin(), g.keySel.end());
    history.commitBefore(root);
    int n = 0;
    if (req == KR::Move) {
        n = moveKeys(*selected, frames, g.keyReqDelta);
        std::set<int> moved;
        for (int f : g.keySel) moved.insert(f + g.keyReqDelta);
        g.keySel = moved;
        flashStatus(tr("Claves movidas ") + std::to_string(g.keyReqDelta) + " F (" + std::to_string(n) + ").");
    } else if (req == KR::Delete) {
        n = deleteKeys(*selected, frames);
        g.keySel.clear();
        flashStatus(tr("Eliminadas ") + std::to_string(n) + (n == 1 ? tr(" clave.") : tr(" claves.")));
    } else {
        n = setKeyInterp(*selected, frames, g.keyReqInterp);
        const char* names[] = {tr("Spline"), tr("Lineal"), tr("Escalón")};
        flashStatus(std::string(tr("Interpolación ")) + names[(int)g.keyReqInterp] + tr(" en ") + std::to_string(n) +
                    (n == 1 ? tr(" clave.") : tr(" claves.")));
    }
    g.lastEvalFrame = -1e9f;  // que la escena tome ya los valores nuevos
    g.selKeys = keyFrames(*selected);
}

// Editor de Curvas: las pistas del objeto activo como curvas valor/fotograma.
// Posicion en cm, rotacion en grados (H/P/B), escala en factor; el eje
// vertical se ajusta a los canales visibles. Clic en una clave la selecciona
// (Mayus anade); la interpolacion se cambia con los botones o el clic derecho.
void drawCurveEditor(SceneNode* selected) {
    g.curvesHovered = false;
    if (!g.showCurves) return;
    ImGui::SetNextWindowSize(ImVec2(760, 340), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(300, 300), ImGuiCond_FirstUseEver);
    bool open = true;
    if (!ImGui::Begin(tr("Editor de Curvas###curves"), &open, ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        if (!open) g.showCurves = false;
        return;
    }
    g.curvesHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);
    static const char* kChan[9] = {tr("Pos. X"), tr("Pos. Y"), tr("Pos. Z"), tr("Rot. H"), tr("Rot. P"), tr("Rot. B"), tr("Esc. X"), tr("Esc. Y"), tr("Esc. Z")};
    static const ImU32 kCol[9] = {IM_COL32(230, 80, 80, 255), IM_COL32(110, 210, 90, 255), IM_COL32(90, 140, 240, 255),
                                  IM_COL32(240, 160, 70, 255), IM_COL32(200, 110, 220, 255), IM_COL32(80, 210, 210, 255),
                                  IM_COL32(230, 130, 130, 255), IM_COL32(150, 220, 130, 255), IM_COL32(130, 170, 240, 255)};
    // Canal i -> (pista, componente). Rotacion: H = y, P = x, B = z.
    auto trackOf = [&](int i) -> const AnimTrack* {
        if (!selected) return nullptr;
        return i < 3 ? &selected->animPos : i < 6 ? &selected->animRot : &selected->animScale;
    };
    auto compOf = [](int i) { static const int c[9] = {0, 1, 2, 1, 0, 2, 0, 1, 2}; return c[i]; };
    auto unitOf = [](int i) { return i < 3 ? kCmPerUnit : 1.0f; };

    for (int i = 0; i < 9; ++i) {
        if (i % 3) ImGui::SameLine();
        else if (i) ImGui::SameLine(0, 18);
        ImGui::PushStyleColor(ImGuiCol_Text, kCol[i]);
        ImGui::Checkbox(kChan[i], &g.curveChan[i]);
        ImGui::PopStyleColor();
    }
    ImGui::BeginDisabled(g.keySel.empty());
    if (ImGui::Button(tr("Spline"))) { g.keyReq = UiState::KeyReq::Interp; g.keyReqInterp = KeyInterp::Spline; }
    ImGui::SameLine();
    if (ImGui::Button(tr("Lineal"))) { g.keyReq = UiState::KeyReq::Interp; g.keyReqInterp = KeyInterp::Linear; }
    ImGui::SameLine();
    if (ImGui::Button(tr("Escalón"))) { g.keyReq = UiState::KeyReq::Interp; g.keyReqInterp = KeyInterp::Step; }
    ImGui::SameLine();
    if (ImGui::Button(tr("Eliminar"))) g.keyReq = UiState::KeyReq::Delete;
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextDisabled(selected ? (g.selKeys.empty() ? tr("%s: sin claves (F9 graba)") : "%s") : tr("Sin objeto activo"),
                        selected ? selected->name().c_str() : "");

    const ImVec2 c0 = ImGui::GetCursorScreenPos();
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const ImVec2 c1(c0.x + std::max(50.0f, avail.x), c0.y + std::max(50.0f, avail.y));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(c0, c1, IM_COL32(38, 38, 38, 255));
    dl->AddRect(c0, c1, kBorderDark);
    const float lm = 46.0f, pad = 10.0f;
    const float x0 = c0.x + lm, x1 = c1.x - pad, y0 = c0.y + pad, y1 = c1.y - 18.0f;
    const int span = std::max(1, g.maxFrame - g.minFrame);
    auto fx = [&](float f) { return x0 + (f - (float)g.minFrame) / (float)span * (x1 - x0); };

    // Rango vertical de los canales visibles (claves + curva muestreada).
    float vmin = 1e30f, vmax = -1e30f;
    for (int i = 0; i < 9; ++i) {
        const AnimTrack* t = trackOf(i);
        if (!g.curveChan[i] || !t || t->empty()) continue;
        for (int f = g.minFrame; f <= g.maxFrame; f += std::max(1, span / 200)) {
            const float v = t->evaluate((float)f)[compOf(i)] * unitOf(i);
            vmin = std::min(vmin, v);
            vmax = std::max(vmax, v);
        }
    }
    if (vmin > vmax) { vmin = -1.0f; vmax = 1.0f; }
    if (vmax - vmin < 1e-3f) { vmin -= 1.0f; vmax += 1.0f; }
    const float m = (vmax - vmin) * 0.08f;
    vmin -= m;
    vmax += m;
    auto fy = [&](float v) { return y1 - (v - vmin) / (vmax - vmin) * (y1 - y0); };

    dl->PushClipRect(c0, c1, true);
    // Rejilla: 0 y 4 divisiones, fotogramas cada pocos.
    for (int k = 0; k <= 4; ++k) {
        const float v = vmin + (vmax - vmin) * (float)k / 4.0f;
        dl->AddLine(ImVec2(x0, fy(v)), ImVec2(x1, fy(v)), IM_COL32(60, 60, 60, 255));
        char buf[24];
        std::snprintf(buf, sizeof(buf), "%.1f", v);
        dl->AddText(ImVec2(c0.x + 4, fy(v) - 7), IM_COL32(150, 150, 150, 255), buf);
    }
    int step = 5;
    while (step * (x1 - x0) / (float)span < 40.0f) step *= 2;
    for (int f = (g.minFrame / step) * step; f <= g.maxFrame; f += step) {
        if (f < g.minFrame) continue;
        dl->AddLine(ImVec2(fx((float)f), y0), ImVec2(fx((float)f), y1), IM_COL32(52, 52, 52, 255));
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%d", f);
        dl->AddText(ImVec2(fx((float)f) + 2, y1 + 2), IM_COL32(150, 150, 150, 255), buf);
    }
    dl->AddLine(ImVec2(fx(g.frame), y0), ImVec2(fx(g.frame), y1), kPlayhead, 2.0f);

    // Curvas y claves.
    int clickedKey = INT32_MIN;
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    for (int i = 0; i < 9; ++i) {
        const AnimTrack* t = trackOf(i);
        if (!g.curveChan[i] || !t || t->empty()) continue;
        const int ci = compOf(i);
        ImVec2 prev;
        for (float px = x0; px <= x1; px += 2.0f) {
            const float f = (float)g.minFrame + (px - x0) / (x1 - x0) * (float)span;
            const ImVec2 q(px, fy(t->evaluate(f)[ci] * unitOf(i)));
            if (px > x0) dl->AddLine(prev, q, kCol[i], 1.5f);
            prev = q;
        }
        for (const AnimKey& k : t->keys) {
            if (k.frame < g.minFrame || k.frame > g.maxFrame) continue;
            const ImVec2 q(fx((float)k.frame), fy(k.value[ci] * unitOf(i)));
            const bool sel = g.keySel.count(k.frame) > 0;
            const float r = sel ? 4.5f : 3.5f;
            dl->AddRectFilled(ImVec2(q.x - r, q.y - r), ImVec2(q.x + r, q.y + r), sel ? IM_COL32(255, 150, 40, 255) : kCol[i]);
            dl->AddRect(ImVec2(q.x - r, q.y - r), ImVec2(q.x + r, q.y + r), IM_COL32(15, 15, 15, 255));
            if (k.interp != KeyInterp::Spline)
                dl->AddText(ImVec2(q.x + 6, q.y - 16), IM_COL32(200, 200, 200, 255), k.interp == KeyInterp::Linear ? "L" : "E");
            if (std::fabs(mouse.x - q.x) <= r + 2 && std::fabs(mouse.y - q.y) <= r + 2) clickedKey = k.frame;
        }
    }
    dl->PopClipRect();
    ImGui::SetCursorScreenPos(c0);
    ImGui::InvisibleButton("##curvecanvas", ImVec2(c1.x - c0.x, c1.y - c0.y));
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
        if (clickedKey != INT32_MIN) {
            if (ImGui::GetIO().KeyShift) {
                if (!g.keySel.erase(clickedKey)) g.keySel.insert(clickedKey);
            } else {
                g.keySel = {clickedKey};
            }
        } else if (!ImGui::GetIO().KeyShift) {
            g.keySel.clear();
        }
    }
    if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
        if (clickedKey != INT32_MIN && !g.keySel.count(clickedKey)) g.keySel = {clickedKey};
        ImGui::OpenPopup("##curvectx");
    }
    if (ImGui::BeginPopup("##curvectx")) {
        drawKeyMenuItems();
        ImGui::EndPopup();
    }
    ImGui::End();
    if (!open) g.showCurves = false;
}

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

    // Arrastrar en la regla mueve el fotograma actual (las claves, dibujadas
    // despues, tienen prioridad sobre ella).
    g.timelineHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);
    ImGui::SetCursorScreenPos(r0);
    ImGui::SetNextItemAllowOverlap();
    ImGui::InvisibleButton("##ruler", ImVec2(r1.x - r0.x, r1.y - r0.y));
    if (ImGui::IsItemActive()) {
        const float t = (ImGui::GetIO().MousePos.x - x0) / std::max(1.0f, x1 - x0);
        g.frame = std::round(std::clamp((float)g.minFrame + t * (float)span, (float)g.minFrame, (float)g.maxFrame));
        g.playDir = 0;
    }
    if (ImGui::IsItemHovered()) g.hint = tr("Línea de Tiempo: haga clic y arrastre para cambiar el fotograma actual.");

    // Claves del objeto activo: cuadraditos sobre la regla, como en C4D. Clic
    // selecciona (Mayus anade/quita), arrastrar mueve las seleccionadas, clic
    // derecho = interpolacion / eliminar, Supr con el raton en la Linea de
    // Tiempo borra.
    for (int k : g.selKeys) {
        if (k < g.minFrame || k > g.maxFrame) continue;
        const bool isSel = g.keySel.count(k) > 0;
        const float x = fx((float)(k + (isSel ? g.keyDragDelta : 0)));
        const float hw = std::max(2.0f, std::min(5.0f, pxPerFrame * 0.5f));
        dl->AddRectFilled(ImVec2(x - hw, r1.y - 9), ImVec2(x + hw, r1.y - 1),
                          isSel ? IM_COL32(255, 150, 40, 255) : IM_COL32(110, 160, 230, 255));
        dl->AddRect(ImVec2(x - hw, r1.y - 9), ImVec2(x + hw, r1.y - 1), IM_COL32(20, 30, 50, 255));
        ImGui::PushID(k);
        ImGui::SetCursorScreenPos(ImVec2(fx((float)k) - hw - 1, r1.y - 11));
        ImGui::InvisibleButton("##key", ImVec2(2 * hw + 2, 11));
        if (ImGui::IsItemHovered())
            g.hint = tr("Clave: clic para seleccionar (Mayús añade), arrastrar para moverla, clic derecho para la interpolación.");
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
            if (ImGui::GetIO().KeyShift) {
                if (!g.keySel.erase(k)) g.keySel.insert(k);
            } else if (!isSel) {
                g.keySel = {k};
            }
            g.keyDragStartX = ImGui::GetIO().MousePos.x;
            g.keyDragDelta = 0;
        }
        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 2.0f) && g.keySel.count(k))
            g.keyDragDelta = (int)std::lround((ImGui::GetIO().MousePos.x - g.keyDragStartX) / std::max(0.01f, pxPerFrame));
        if (ImGui::IsItemDeactivated() && g.keyDragDelta != 0) {
            g.keyReq = UiState::KeyReq::Move;
            g.keyReqDelta = g.keyDragDelta;
            g.keyDragDelta = 0;
        }
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
            if (!isSel) g.keySel = {k};
            ImGui::OpenPopup("##keyctx");
        }
        ImGui::PopID();
    }
    if (ImGui::BeginPopup("##keyctx")) {
        drawKeyMenuItems();
        ImGui::EndPopup();
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
    setHint(tr("Tiempo Mínimo del proyecto."));
    ImGui::SameLine();
    {
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float w = std::max(40.0f, size.x * 0.22f);
        ImGui::InvisibleButton("##range", ImVec2(w, b));
        dl->AddRectFilled(ImVec2(p.x, p.y + b * 0.3f), ImVec2(p.x + w, p.y + b * 0.7f), IM_COL32(40, 40, 40, 255), 2);
        const float a0 = p.x + (float)(g.previewMin - g.minFrame) / (float)span * w;
        const float a1 = p.x + (float)(g.previewMax - g.minFrame) / (float)span * w;
        dl->AddRectFilled(ImVec2(a0, p.y + b * 0.3f), ImVec2(a1, p.y + b * 0.7f), IM_COL32(120, 120, 120, 255), 2);
        setHint(tr("Rango de previo (Tiempo Mínimo/Máximo de Previo)."));
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(56);
    ImGui::DragInt("##tmax", &g.maxFrame, 0.2f, g.minFrame + 1, 100000, "%d F");
    setHint(tr("Tiempo Máximo del proyecto (editable, la regla se reescala)."));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(56);
    ImGui::DragInt("##pmax2", &g.previewMax, 0.2f, g.previewMin, g.maxFrame, "%d F");
    setHint(tr("Tiempo Máximo de Previo."));
    ImGui::SameLine(0, 14);

    auto tbtn = [&](const char* id, Icon icon, bool active, bool enabled, const char* tip) {
        const bool clicked = iconButton(id, icon, b, active, enabled, tip, tip) == BtnResult::Click;
        ImGui::SameLine();
        return clicked;
    };
    if (tbtn("tstart", Icon::ToStart, false, true, tr("Ir al Inicio [Mayús+F]"))) g.frame = (float)g.minFrame;
    if (tbtn("tprevk", Icon::PrevKey, false, !g.selKeys.empty(), tr("Ir a la Clave Anterior [Ctrl+F]"))) jumpToKey(-1);
    if (tbtn("tprev", Icon::StepBack, false, true, tr("Ir al Fotograma Anterior [F]"))) g.frame = std::max((float)g.minFrame, std::ceil(g.frame) - 1);
    if (tbtn("tplayb", Icon::PlayBack, g.playDir == -1, true, tr("Reproducir Hacia Atrás [F6]"))) g.playDir = g.playDir == -1 ? 0 : -1;
    if (tbtn("tplay", g.playDir == 1 ? Icon::Pause : Icon::Play, g.playDir == 1, true, tr("Reproducir Hacia Delante [F8]"))) g.playDir = g.playDir == 1 ? 0 : 1;
    if (tbtn("tnext", Icon::StepFwd, false, true, tr("Ir al Fotograma Siguiente [G]"))) g.frame = std::min((float)g.maxFrame, std::floor(g.frame) + 1);
    if (tbtn("tnextk", Icon::NextKey, false, !g.selKeys.empty(), tr("Ir a la Clave Siguiente [Ctrl+G]"))) jumpToKey(1);
    if (tbtn("tend", Icon::ToEnd, false, true, tr("Ir al Final [Mayús+G]"))) g.frame = (float)g.maxFrame;
    toolbarSeparator(true, b);
    ImGui::SameLine();
    if (tbtn("trec", Icon::Record, false, true, tr("Grabar Objetos Activos [F9]"))) g.recordRequested = true;
    if (tbtn("tauto", Icon::AutoKey, g.autoKey, true, tr("Fotogramas Clave Automáticos [Ctrl+F9]"))) g.autoKey = !g.autoKey;
    tbtn("tksel", Icon::KeySel, false, false, tr("Selección de Claves (todavía no implementado)"));
    toolbarSeparator(true, b);
    ImGui::SameLine();
    if (tbtn("tkpos", Icon::KeyPos, g.keyPos, true, tr("Grabar Posición"))) g.keyPos = !g.keyPos;
    if (tbtn("tkscl", Icon::KeyScale, g.keyScl, true, tr("Grabar Escala"))) g.keyScl = !g.keyScl;
    if (tbtn("tkrot", Icon::KeyRot, g.keyRot, true, tr("Grabar Rotación"))) g.keyRot = !g.keyRot;
    tbtn("tkpar", Icon::KeyParam, false, false, tr("Grabar Parámetro (todavía no implementado)"));
    tbtn("tkpla", Icon::KeyPLA, false, false, tr("Grabar Nivel de Puntos (todavía no implementado)"));
    ImGui::PopStyleVar();
    // Panel mas alto que la tira de transporte (SFM): el resto es donde ira
    // el editor de curvas; de momento se dice tal cual.
    if (size.y > 110.0f) {
        const char* msg = tr("Editor de Gráficos / Hoja de Claves: todavía no implementado en Iron3D");
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
        panelGrip(tr(names().materials));
        if (ImGui::BeginMenu(tr("Crear"))) {
            ImGui::MenuItem(tr("Nuevo Material PBR"), tr("Ctrl+Mayús+N"), false, false);
            if (ImGui::MenuItem(tr("Nuevo Material"), tr("Ctrl+N"))) {
                history.commitBefore(root);
                newMaterial(materials);
            }
            ImGui::BeginMenu(tr("Shader"), false);
            ImGui::Separator();
            disabledItems({{tr("Cargar Materiales..."), tr("Ctrl+Mayús+O")}, {tr("Guardar Materiales como..."), ""},
                           {tr("Guardar Todos los Materiales como..."), ""}, {nullptr, nullptr},
                           {tr("Cargar Material Preestablecido"), ""}, {tr("Guardar Material Preestablecido..."), ""}});
            ImGui::EndMenu();
        }
        const bool hasSel = g.selectedMat >= 0 && (size_t)g.selectedMat < materials.size();
        if (ImGui::BeginMenu(tr("Edición"))) {
            ImGui::MenuItem(tr("Deshacer"), tr("Ctrl+Z"), false, false);
            ImGui::Separator();
            if (ImGui::MenuItem(tr("Eliminar"), tr("Supr"), false, hasSel)) {
                history.commitBefore(root);
                fixupMaterialIndicesAfterRemoval(root, g.selectedMat);
                materials.erase(materials.begin() + g.selectedMat);
                g.selectedMat = -1;
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu(tr("Función"))) {
            if (ImGui::MenuItem(tr("Editar..."), nullptr, false, hasSel)) g.showMatEditor = true;
            if (ImGui::MenuItem(tr("Renombrar"), nullptr, false, hasSel)) {
                g.renamingMat = g.selectedMat;
                std::snprintf(g.renameBuf, sizeof(g.renameBuf), "%s", materials[(size_t)g.selectedMat].name.c_str());
                g.renameFocus = true;
            }
            if (ImGui::MenuItem(tr("Aplicar"), nullptr, false, hasSel && selected && hasGeometry(selected->primitive))) {
                history.commitBefore(root);
                selected->materialIndex = g.selectedMat;
            }
            ImGui::Separator();
            if (ImGui::MenuItem(tr("Eliminar Materiales sin Usar"), nullptr, false, !materials.empty())) {
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
        if (ImGui::BeginMenu(tr("Textura"))) {
            disabledItems({{tr("Cargar Textura..."), ""}, {tr("Editar Textura..."), ""}});
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
        if (hov) g.hint = tr("Clic: seleccionar material. Doble clic: Editor de Material (en el nombre: renombrar). Arrastre a un objeto para asignarlo.");
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
            if (ImGui::MenuItem(tr("Editar..."))) g.showMatEditor = true;
            if (ImGui::MenuItem(tr("Aplicar"), nullptr, false, selected && hasGeometry(selected->primitive))) {
                history.commitBefore(root);
                selected->materialIndex = i;
            }
            if (ImGui::MenuItem(tr("Renombrar"))) {
                g.renamingMat = i;
                std::snprintf(g.renameBuf, sizeof(g.renameBuf), "%s", m.name.c_str());
                g.renameFocus = true;
            }
            if (ImGui::MenuItem(tr("Eliminar"), tr("Supr"))) pendingDelete = i;
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
        if (ImGui::IsItemHovered()) g.hint = tr("Doble clic en un espacio vacío del Gestor de Materiales para crear un material nuevo.");
        if (materials.empty()) {
            const char* t = tr("Doble clic aquí para crear un material");
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
    panelGrip(chipLine ? tr(names().coords) : nullptr);
    const float colW = (ImGui::GetContentRegionAvail().x - 6) / 3.0f;
    const char* headers[] = {tr("Posición"), tr("Tamaño"), tr("Rotación")};
    if (g.coordSizeMode == 1) headers[1] = tr("Escala");
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
                if (ImGui::DragFloat("##v", &cm, 1.0f, 0, 0, tr("%.0f cm"))) posRef[i] = cm / kCmPerUnit;
            } else if (c == 1) {
                if (g.coordSizeMode == 0 && base[i] > 0.0f) {
                    float sz = sclRef[i] * base[i];
                    if (ImGui::DragFloat("##v", &sz, 1.0f, 0.1f, 1.0e6f, tr("%.0f cm"))) sclRef[i] = std::max(0.0005f, sz / base[i]);
                } else if (g.coordSizeMode == 0) {
                    float zero = 0.0f;
                    ImGui::BeginDisabled();
                    ImGui::DragFloat("##v", &zero, 1.0f, 0, 0, tr("%.0f cm"));
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
    const char* spaces[] = {tr("Objeto (Rel)"), tr("Objeto (Abs)"), tr("Mundo")};
    if (ImGui::BeginCombo("##space", spaces[g.coordSpace])) {
        for (int i = 0; i < 3; ++i) {
            if (i > 0) ImGui::BeginDisabled();
            if (ImGui::Selectable(spaces[i], g.coordSpace == i)) g.coordSpace = i;
            if (i > 0) ImGui::EndDisabled();
        }
        ImGui::EndCombo();
    }
    setHint(tr("Sistema de coordenadas de los campos (solo Objeto (Rel) disponible por ahora)."));
    ImGui::SameLine(baseX + colW + 14);
    ImGui::SetNextItemWidth(colW - 14);
    const char* sizeModes[] = {tr("Tamaño"), tr("Escala")};
    ImGui::Combo("##sizemode", &g.coordSizeMode, sizeModes, 2);
    setHint(tr("Tamaño: medidas en cm. Escala: multiplicador del objeto."));
    ImGui::SameLine(baseX + 2 * colW + 14);
    if (!selected) ImGui::BeginDisabled();
    if (ImGui::Button(tr("Aplicar"), ImVec2(colW - 14, 0)) && selected) history.commitBefore(root);
    if (!selected) ImGui::EndDisabled();
    setHint(tr("Aplicar los valores de los campos al objeto seleccionado."));
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
            text = tr("Selección Rectangular: arrastre para seleccionar los objetos que toque el rectángulo. MAYÚSCULAS añade, CTRL quita.");
        else if (vp.selectTool && vp.selectShape == 2)
            text = tr("Selección con Lazo: dibuje una zona para seleccionar los objetos que queden dentro. MAYÚSCULAS añade, CTRL quita.");
        else if (vp.selectTool) text = tr("Selección en Vivo: Haga clic para seleccionar elementos.");
        else if (mode == GizmoMode::Translate)
            text = tr("Mover: Haga clic y arrastre para mover elementos. Pulse MAYÚSCULAS para cuantificar el movimiento / añadir a la selección en el modo punto, CTRL para eliminar.");
        else if (mode == GizmoMode::Rotate)
            text = tr("Rotar: Haga clic y arrastre para rotar elementos. Pulse MAYÚSCULAS para cuantificar la rotación / añadir a la selección en el modo punto, CTRL para eliminar.");
        else
            text = tr("Escalar: Haga clic y arrastre para escalar elementos. Pulse MAYÚSCULAS para cuantificar el escalado / añadir a la selección en el modo punto, CTRL para eliminar.");
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
    if (ImGui::Begin(g.sceneLoadMode ? tr("Abrir###scenedlg") : tr("Guardar como###scenedlg"), &g.showScene,
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted(tr("Nombre de archivo:"));
        ImGui::SetNextItemWidth(300);
        ImGui::InputText("##fn", filenameBuf, sizeof(filenameBuf));
        ImGui::SameLine();
        ImGui::TextDisabled(".iron");
        ImGui::TextDisabled(tr("Carpeta: %s"), scenesDir.string().c_str());
        ImGui::Separator();
        const std::filesystem::path path = scenesDir / (std::string(filenameBuf) + ".iron");
        if (ImGui::Button(g.sceneLoadMode ? tr("Abrir") : tr("Guardar"), ImVec2(120, 0))) {
            std::string err;
            if (g.sceneLoadMode) {
                if (loadScene(path, root, materials, &err)) {
                    selected = nullptr;
                    history.reset();
                    g.selectedMat = -1;
                    g.docPath = path;
                    g.cleanInit = false;
                    statusMsg = tr("Cargado: ") + path.filename().string();
                    statusIsError = false;
                } else {
                    statusMsg = tr("Error al cargar: ") + err;
                    statusIsError = true;
                }
            } else if (saveScene(path, root, materials, &err)) {
                g.docPath = path;
                g.cleanSnap = history.snapshot(root);
                g.cleanInit = true;
                statusMsg = tr("Guardado: ") + path.filename().string();
                statusIsError = false;
            } else {
                statusMsg = tr("Error al guardar: ") + err;
                statusIsError = true;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button(tr("Cancelar"), ImVec2(120, 0))) g.showScene = false;
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
    if (hov) ImGui::SetTooltip(tr("%s\nDisposición: %s"), p.name.c_str(), arrangementName(p.layout.arrangement));
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
    {
        // Idioma de la interfaz: se aplica al elegirlo (sin reiniciar) y se guarda para la proxima vez.
        const std::string active = i18n::currentLanguage();
        const std::string chosen = prefs.language.empty() ? active : prefs.language;
        const LanguageInfo* ci = i18n::findLanguage(chosen);
        ImGui::TextUnformatted(tr("Idioma"));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(220);
        if (ImGui::BeginCombo("##uilang", ci ? ci->nativeName : chosen.c_str())) {
            for (const LanguageInfo& li : i18n::languages()) {
                if (ImGui::Selectable(li.nativeName, chosen == li.code)) {
                    prefs.language = li.code;
                    i18n::setLanguage(li.code);
                    statusMsg = savePreferences(prefsPath, prefs) ? std::string() : std::string(tr("Error al guardar las preferencias."));
                }
            }
            ImGui::EndCombo();
        }
        ImGui::Separator();
    }
    auto choose = [&](const UiPreset& p) {
        cur = p;
        applyPreset(cur);
        statusMsg.clear();
    };

    ImGui::TextDisabled(tr("Colores y disposición de los paneles como en cada programa. Se aplica al momento."));
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
        ImGui::TextDisabled(tr("Mis presets"));
        col = 0;
        for (const UiPreset& p : userPresets) {
            place();
            if (presetCard(p.id.c_str(), p, cur.id == p.id, cardW)) choose(p);
        }
    }

    ImGui::Dummy(ImVec2(0, 6));
    sectionHeader(tr("Personalizar"));
    bool changed = false;
    // Disposicion base: cambiarla trae la colocacion y las metricas de ese
    // programa, pero conserva los colores que haya.
    int arr = (int)cur.layout.arrangement;
    const char* arrNames[] = {tr("Cinema 4D"), tr("Blender"), tr("Maya"), tr("3ds Max"), tr("Source Filmmaker")};
    ImGui::SetNextItemWidth(220);
    if (ImGui::Combo(tr("Disposición"), &arr, arrNames, IM_ARRAYSIZE(arrNames))) {
        const UiPreset& base = builtinForArrangement((Arrangement)arr);
        cur.layout = base.layout;
        cur.style = base.style;
        changed = true;
    }
    if (ImGui::BeginTable("##cust", 2, ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableNextColumn();
        ImGui::TextDisabled(tr("Colores"));
        changed |= ImGui::ColorEdit3(tr("Fondo de paneles"), &cur.theme.panelBg.x, ImGuiColorEditFlags_NoInputs);
        changed |= ImGui::ColorEdit3(tr("Fondo de campos"), &cur.theme.fieldBg.x, ImGuiColorEditFlags_NoInputs);
        changed |= ImGui::ColorEdit3(tr("Acento"), &cur.theme.accent.x, ImGuiColorEditFlags_NoInputs);
        changed |= ImGui::ColorEdit3(tr("Azul de herramientas"), &cur.theme.toolBlue.x, ImGuiColorEditFlags_NoInputs);
        changed |= ImGui::ColorEdit3(tr("Texto"), &cur.theme.text.x, ImGuiColorEditFlags_NoInputs);
        changed |= ImGui::ColorEdit3(tr("Selección"), &cur.theme.selection.x, ImGuiColorEditFlags_NoInputs);
        changed |= ImGui::ColorEdit3(tr("Fondo del visor"), &cur.theme.viewportBg.x, ImGuiColorEditFlags_NoInputs);
        ImGui::TableNextColumn();
        ImGui::TextDisabled(tr("Paneles"));
        LayoutSpec& l = cur.layout;
        const float sw = std::max(80.0f, ImGui::GetContentRegionAvail().x - 150.0f);
        ImGui::SetNextItemWidth(sw);
        changed |= ImGui::SliderFloat(tr("Barra superior"), &l.toolbarH, 26.0f, 48.0f, tr("%.0f px"));
        ImGui::SetNextItemWidth(sw);
        if (ImGui::SliderFloat(tr("Columna lateral"), &l.sideFrac, 0.15f, 0.40f, "%.2f")) {
            l.sideMin = std::min(l.sideMin, 220.0f); // que el minimo no anule el deslizador
            changed = true;
        }
        if (l.arrangement == Arrangement::Maya || l.arrangement == Arrangement::Max) {
            ImGui::SetNextItemWidth(sw);
            changed |= ImGui::SliderFloat(tr("Columna de objetos"), &l.outlinerW, 150.0f, 360.0f, tr("%.0f px"));
        } else {
            ImGui::SetNextItemWidth(sw);
            changed |= ImGui::SliderFloat(tr("Objetos / Atributos"), &l.objFrac, 0.20f, 0.80f, "%.2f");
        }
        ImGui::SetNextItemWidth(sw);
        changed |= ImGui::SliderFloat(tr("Fila inferior"), &l.shelfFrac, 0.08f, 0.25f, "%.2f");
        ImGui::SetNextItemWidth(sw);
        if (l.timelineFrac > 0.0f) changed |= ImGui::SliderFloat(tr("Línea de tiempo"), &l.timelineFrac, 0.12f, 0.50f, "%.2f");
        else changed |= ImGui::SliderFloat(tr("Línea de tiempo"), &l.timelineH, 36.0f, 96.0f, tr("%.0f px"));
        changed |= ImGui::Checkbox(tr("Pestañas verticales"), &l.verticalTabs);
        ImGui::SetNextItemWidth(sw);
        changed |= ImGui::SliderFloat(tr("Redondeo"), &cur.style.frameRounding, 0.0f, 8.0f, tr("%.0f px"));
        if (changed) {
            cur.style.grabRounding = cur.style.frameRounding;
            cur.style.popupRounding = std::max(cur.style.popupRounding, 0.0f);
        }
        ImGui::EndTable();
    }
    if (changed) {
        // Tocar un preset lo convierte en uno propio sin guardar.
        cur.id = "custom";
        cur.name = tr("Personalizado (sin guardar)");
        applyPreset(cur);
        statusMsg.clear();
    }

    ImGui::Dummy(ImVec2(0, 4));
    static char newName[64] = "";
    ImGui::SetNextItemWidth(200);
    ImGui::InputTextWithHint("##pname", tr("Nombre del preset"), newName, sizeof(newName));
    ImGui::SameLine();
    if (ImGui::Button(tr("Guardar como preset"))) {
        UiPreset p = cur;
        p.name = newName;
        if (saveUserPreset(presetsDir, p)) {
            userPresets = loadUserPresets(presetsDir);
            for (const UiPreset& u : userPresets)
                if (u.name == p.name || u.id == "user:" + std::string(newName)) cur = u;
            statusMsg = tr("Preset guardado en presets/.");
            newName[0] = 0;
        } else {
            statusMsg = tr("Escriba un nombre válido (letras, números, espacios, - o _).");
        }
    }
    const bool isUser = cur.id.rfind("user:", 0) == 0;
    ImGui::SameLine();
    if (!isUser) ImGui::BeginDisabled();
    if (ImGui::Button(tr("Eliminar"))) {
        if (deleteUserPreset(presetsDir, cur.name)) {
            userPresets = loadUserPresets(presetsDir);
            choose(resetTargetFor(cur));
            statusMsg = tr("Preset eliminado.");
        }
    }
    if (!isUser) ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button(tr("Restablecer"))) choose(resetTargetFor(cur));
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(tr("Vuelve al preset integrado «%s»."), resetTargetFor(cur).name.c_str());

    ImGui::Dummy(ImVec2(0, 4));
    if (ImGui::Button(tr("Guardar##prefsui"), ImVec2(120, 0))) {
        statusMsg = savePreferences(prefsPath, prefs) ? tr("Guardado. Se usará al abrir Iron3D.")
                                                      : tr("Error al guardar las preferencias.");
    }
    ImGui::SameLine();
    ImGui::TextDisabled(tr("Preset activo: %s"), cur.name.c_str());
    if (!statusMsg.empty()) ImGui::TextUnformatted(statusMsg.c_str());
}

// Preferencias > Navegacion: lo que de verdad cambia el visor (velocidades y orbita invertida).
void drawNavigationPage(Preferences& prefs, const std::filesystem::path& prefsPath, std::string& statusMsg) {
    bool changed = false;
    auto row = [&](const char* label) {
        ImGui::TextUnformatted(label);
        ImGui::SameLine(260.0f);
        ImGui::SetNextItemWidth(120.0f);
    };
    row(tr("Invertir Órbita"));
    changed |= ImGui::Checkbox("##invorb", &prefs.navInvertOrbit);
    ImGui::Separator();
    row(tr("Velocidad Pan"));
    ImGui::DragInt("##navpan", &prefs.navPan, 1.0f, 10, 500, "%d %%");
    changed |= ImGui::IsItemDeactivatedAfterEdit();
    row(tr("Velocidad Muñeca/Zoom"));
    ImGui::DragInt("##navzoom", &prefs.navZoom, 1.0f, 10, 500, "%d %%");
    changed |= ImGui::IsItemDeactivatedAfterEdit();
    row(tr("Velocidad Órbita"));
    ImGui::DragInt("##navorbit", &prefs.navOrbit, 1.0f, 10, 500, "%d %%");
    changed |= ImGui::IsItemDeactivatedAfterEdit();
    prefs.navPan = std::clamp(prefs.navPan, 10, 500);
    prefs.navZoom = std::clamp(prefs.navZoom, 10, 500);
    prefs.navOrbit = std::clamp(prefs.navOrbit, 10, 500);
    if (changed) statusMsg = savePreferences(prefsPath, prefs) ? std::string() : std::string(tr("Error al guardar las preferencias."));
    ImGui::Dummy(ImVec2(0, 6));
    ImGui::TextDisabled(tr("Se aplica al momento. Alt+clic orbita, panea y hace zoom; la rueda acerca y aleja."));
}

void drawPreferencesDialog(RendererBackend activeBackend, Preferences& prefs, const std::filesystem::path& prefsPath) {
    if (!g.showPrefs) return;
    static std::string statusMsg;
    ImGui::SetNextWindowSize(ImVec2(900, 640), ImGuiCond_Appearing);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::Begin(tr("Preferencias"), &g.showPrefs, ImGuiWindowFlags_NoCollapse)) {
        // Las paginas de Cinema 4D R19; solo salen activas las que cambian algo en Iron3D.
        struct Page { const char* name; bool usable; };
        static const Page pages[] = {
            {tr("Interfaz"), true},         {tr("Dispositivos de Entrada"), false}, {tr("Navegación"), true},
            {tr("OpenGL"), true},           {tr("Vista"), false},                   {tr("Archivos"), false},
            {tr("Unidades"), false},        {tr("Memoria"), false},                 {tr("Comunicación"), false},
            {tr("Renderizador"), true},     {tr("BodyPaint 3D"), false},            {tr("Escultura"), false},
            {tr("Hair"), false},            {tr("Línea de Tiempo"), false},         {tr("Motor Houdini"), false},
            {tr("Motor Substance"), false}, {tr("Navegador de Contenido"), false},  {tr("Previo del Material"), false},
            {tr("Projection Man"), false},  {tr("Sketch and Toon"), false},         {tr("Tomas"), false},
            {tr("Importación/Exportación"), false}, {tr("Colores Esquema"), false}};
        static int page = 0;
        if (g.prefsPageReq >= 0 && g.prefsPageReq < IM_ARRAYSIZE(pages)) page = g.prefsPageReq;
        g.prefsPageReq = -1;
        const float footerH = ImGui::GetFrameHeight() + 10.0f;
        ImGui::BeginChild("##pages", ImVec2(210, -footerH), ImGuiChildFlags_Border);
        for (int i = 0; i < IM_ARRAYSIZE(pages); ++i) {
            ImGui::BeginDisabled(!pages[i].usable);
            if (ImGui::Selectable(pages[i].name, page == i)) page = i;
            ImGui::EndDisabled();
        }
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("##page", ImVec2(0, -footerH));
        sectionHeader(pages[page].name);
        if (page == 0) {
            drawInterfacePage(prefs, prefsPath, statusMsg);
        } else if (page == 2) {
            drawNavigationPage(prefs, prefsPath, statusMsg);
        } else if (page == 3 || page == 9) {
            ImGui::Text(tr("Backend activo en esta sesión: %s"), activeBackend == RendererBackend::Vulkan ? tr("Vulkan") : tr("OpenGL"));
            ImGui::TextUnformatted(tr("Backend de render (se aplica al reiniciar):"));
            int idx = prefs.renderBackend == RendererBackend::Vulkan ? 0 : 1;
            if (ImGui::RadioButton(tr("Vulkan"), idx == 0)) prefs.renderBackend = RendererBackend::Vulkan;
            ImGui::SameLine();
            if (ImGui::RadioButton(tr("OpenGL"), idx == 1)) prefs.renderBackend = RendererBackend::OpenGL;
            if (ImGui::Button(tr("Guardar"))) {
                statusMsg = savePreferences(prefsPath, prefs) ? tr("Guardado. Reinicie Iron3D Studio para aplicarlo.")
                                                              : tr("Error al guardar las preferencias.");
            }
        }
        if (!statusMsg.empty()) ImGui::TextUnformatted(statusMsg.c_str());
        ImGui::EndChild();
        if (ImGui::Button(tr("Abrir Carpeta de Preferencias..."))) openFolderInExplorer(prefsPath.parent_path());
        ImGui::SameLine();
        ImGui::TextDisabled("%s", prefsPath.parent_path().string().c_str());
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
    std::snprintf(title, sizeof(title), tr("Editor de Material  [%s]###mateditor"), m.name.c_str());
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
        struct Ch { Tr name; bool* flag; bool usable; };
        const Ch channels[] = {{tr("Color"), &m.useColor, true},       {tr("Difusión"), nullptr, false},
                               {"Luminancia", &m.useLuminance, true}, {tr("Transparencia"), &m.useTransparency, true},
                               {"Reflectancia", &m.useReflectance, true}, {tr("Entorno"), nullptr, false},
                               {"Niebla", nullptr, false},        {tr("Relieve"), &m.useBump, true},
                               {"Normales", &m.useNormal, true},  {tr("Alfa"), &m.useAlpha, true},
                               {"Fosforescencia", nullptr, false}, {tr("Desplazamiento"), nullptr, false},
                               {nullptr, nullptr, false},         {tr("Editor"), nullptr, false},
                               {"Iluminación", nullptr, false},   {tr("Asignación"), nullptr, true}};
        for (int i = 0; i < IM_ARRAYSIZE(channels); ++i) {
            if (!channels[i].name) {
                ImGui::Separator();
                continue;
            }
            ImGui::PushID(i);
            bool on = channels[i].flag ? *channels[i].flag : false;
            ImGui::BeginDisabled(!channels[i].flag);
            if (ImGui::Checkbox("##on", &on) && channels[i].flag) {
                history.commitBefore(root);
                *channels[i].flag = on;
                syncMaterialChannels(m);
            }
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
            sectionHeader(tr("Color"));
            colorChannelBlock(m, TexSlot::Color, history, root);
            colorTabExtras(m, history, root);
        } else if (g.matEditorChannel == 2) {
            sectionHeader(tr("Luminancia"));
            colorChannelBlock(m, TexSlot::Emission, history, root);
        } else if (g.matEditorChannel == 3) {
            sectionHeader(tr("Transparencia"));
            colorChannelBlock(m, TexSlot::Transparency, history, root);
        } else if (g.matEditorChannel == 4) {
            reflectancePanel(m, history, root);
        } else if (g.matEditorChannel == 7) {
            sectionHeader(tr("Relieve"));
            plainChannelBlock(m, TexSlot::Bump, history, root);
        } else if (g.matEditorChannel == 8) {
            sectionHeader(tr("Normales"));
            plainChannelBlock(m, TexSlot::Normal, history, root);
        } else if (g.matEditorChannel == 9) {
            sectionHeader(tr("Alfa"));
            plainChannelBlock(m, TexSlot::Alpha, history, root);
        } else {
            sectionHeader(tr("Asignación"));
            std::vector<SceneNode*> users;
            collectUsers(root, g.selectedMat, users);
            if (users.empty()) ImGui::TextDisabled(tr("  Ningún objeto usa este material."));
            for (SceneNode* u : users) ImGui::BulletText("%s", u->name().c_str());
        }
        ImGui::EndChild();
    }
    ImGui::End();
}

// Informacion del Objeto (clic derecho en el Gestor de Objetos): datos del objeto activo, solo lectura.
void drawObjectInfoDialog(const SceneNode* selected) {
    if (!g.showObjInfo) return;
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::Begin(tr("Información del Objeto"), &g.showObjInfo, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse)) {
        if (!selected) {
            ImGui::TextDisabled(tr("No hay ningún objeto seleccionado."));
        } else {
            ImGui::Text("%s", selected->name().c_str());
            ImGui::TextDisabled("%s", primitiveTypeName(selected->primitive));
            ImGui::Separator();
            ImGui::Text("%s: %.2f  %.2f  %.2f cm", tr("Posición"), selected->position.x * kCmPerUnit, selected->position.y * kCmPerUnit,
                        selected->position.z * kCmPerUnit);
            ImGui::Text("%s: %.1f  %.1f  %.1f", tr("Rotación"), selected->rotationDeg.x, selected->rotationDeg.y, selected->rotationDeg.z);
            ImGui::Text("%s: %.3f  %.3f  %.3f", tr("Escala"), selected->scale.x, selected->scale.y, selected->scale.z);
            ImGui::Text("%s: %d", tr("Hijos"), (int)selected->children().size());
            if (selected->mesh) {
                const MeshData& m = *selected->mesh;
                const MeshTopology t = buildMeshTopology(m);
                size_t tri = 0, quad = 0, ng = 0;
                for (const auto& pl : t.polys) (pl.size() == 3 ? tri : pl.size() == 4 ? quad : ng)++;
                ImGui::Separator();
                ImGui::Text("%s: %zu", tr("Puntos"), t.points.size());
                ImGui::Text("%s: %zu", tr("Aristas"), t.edges.size());
                ImGui::Text("%s: %zu (%zu %s, %zu %s, %zu N-gons)", tr("Polígonos"), t.polys.size(), tri, tr("triángulos"), quad, tr("cuadriláteros"), ng);
                const glm::vec3 sz = (m.boundsMax - m.boundsMin) * kCmPerUnit * selected->scale;
                ImGui::Text("%s: %.1f x %.1f x %.1f cm", tr("Tamaño"), std::fabs(sz.x), std::fabs(sz.y), std::fabs(sz.z));
            }
        }
    }
    ImGui::End();
}

void drawAboutDialog() {
    if (!g.showAbout) return;
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::Begin(tr("Acerca de Iron3D Studio"), &g.showAbout, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse)) {
        if (g_fontBold) ImGui::PushFont(g_fontBold);
        ImGui::TextUnformatted(tr("Iron3D Studio"));
        if (g_fontBold) ImGui::PopFont();
        ImGui::TextUnformatted(tr("Static Forge"));
        ImGui::Separator();
        ImGui::TextDisabled(tr("Interfaz inspirada en el flujo de trabajo de Cinema 4D R19."));
        ImGui::TextDisabled(tr("Cinema 4D es una marca de Maxon; Iron3D no está afiliado a Maxon."));
        if (cinewareAvailable()) {
            ImGui::Separator();
            ImGui::TextDisabled(tr("La importación de archivos .c4d usa el Cineware SDK"));
            ImGui::TextDisabled(tr("de MAXON Computer GmbH, integrado según su licencia."));
        }
    }
    ImGui::End();
}

void drawImportReport() {
    if (!g.showImport) return;
    // Verificacion por captura (2026-11-18): el aviso modal tapa el modelo. Solo oculta la ventana.
    if (std::getenv("IRON3D_NO_IMPORT_REPORT")) { g.showImport = false; return; }
    const C4DImportReport& r = g.importReport;
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    const std::string titleStr = (g.importError.empty() ? g.importTitle : std::string(tr("No se pudo abrir"))) + "###importrep";
    const char* title = titleStr.c_str();
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
            ImGui::Text(tr("%d objetos, %d con malla, %d materiales, %d luces"), r.objects, r.meshes, r.materials,
                        r.lights);
            ImGui::Text(tr("%lld puntos, %lld polígonos"), r.points, r.polygons);
            if (r.generatorsFromCache > 0)
                ImGui::Text(tr("%d generadores con la geometría guardada por Cinema 4D"), r.generatorsFromCache);
            if (r.primitivesRebuilt > 0)
                ImGui::Text(tr("%d primitivas reconstruidas desde sus parámetros"), r.primitivesRebuilt);
            ImGui::Separator();
            if (r.warnings.empty()) {
                ImGui::TextColored(ImVec4(0.45f, 0.85f, 0.45f, 1.0f), tr("Importado sin avisos."));
            } else {
                ImGui::TextUnformatted(tr("Revisa:"));
                for (const std::string& w : r.warnings) ImGui::BulletText("%s", w.c_str());
            }
        }
        ImGui::PopTextWrapPos();
        ImGui::Spacing();
        if (ImGui::Button(tr("Cerrar"), ImVec2(120, 0))) g.showImport = false;
    }
    ImGui::End();
}

void drawExportReport() {
    if (!g.showExport) return;
    const C4DExportReport& r = g.exportReport;
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::Begin(tr("Exportado a Cinema 4D###exportrep"), &g.showExport,
                     ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse)) {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 34.0f);
        if (g_fontBold) ImGui::PushFont(g_fontBold);
        ImGui::TextUnformatted(r.fileName.c_str());
        if (g_fontBold) ImGui::PopFont();
        ImGui::TextDisabled("%s", g.exportPath.c_str());
        ImGui::Text(tr("%d objetos: %d mallas, %d primitivas, %d luces"), r.objects, r.meshes, r.primitives, r.lights);
        ImGui::Text(tr("%d materiales, %lld puntos, %lld polígonos"), r.materials, r.points, r.polygons);
        ImGui::Separator();
        if (r.warnings.empty()) {
            ImGui::TextColored(ImVec4(0.45f, 0.85f, 0.45f, 1.0f), tr("Exportado sin avisos."));
        } else {
            ImGui::TextUnformatted(tr("Revisa:"));
            for (const std::string& w : r.warnings) ImGui::BulletText("%s", w.c_str());
        }
        ImGui::PopTextWrapPos();
        ImGui::Spacing();
        if (ImGui::Button(tr("Cerrar"), ImVec2(120, 0))) g.showExport = false;
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

void viewportSelectOnly(SceneNode* node) {
    g.selIds.clear();
    if (node) g.selIds.insert(node->id());
}

bool viewportSelectionContains(const SceneNode* node) {
    return node && (g.selIds.count(node->id()) > 0 || g.lastSelectedId == node->id());
}

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

    // Glifos segun el idioma (core/I18n.h): latino con tildes + extendido (polaco, checo), puntuacion y
    // flechas, y cirilico para ruso. Los idiomas CJK mezclan ademas una fuente del sistema.
    const std::string lang = i18n::currentLanguage();
    static const ImWchar kLatin[] = {0x0020, 0x017F, 0x2000, 0x206F, 0x20A0, 0x20CF, 0x2190, 0x21FF, 0x2260, 0x226F, 0};
    static ImVector<ImWchar> ranges;
    ImFontGlyphRangesBuilder builder;
    builder.AddRanges(kLatin);
    if (lang == "ru") builder.AddRanges(io.Fonts->GetGlyphRangesCyrillic());
    builder.BuildRanges(&ranges);

    const ImWchar* cjkRanges = nullptr;
    std::vector<const char*> cjkFonts;
    if (lang == "ja") {
        cjkRanges = io.Fonts->GetGlyphRangesJapanese();
        cjkFonts = {"C:/Windows/Fonts/YuGothM.ttc", "C:/Windows/Fonts/meiryo.ttc", "C:/Windows/Fonts/msgothic.ttc"};
    } else if (lang == "ko") {
        cjkRanges = io.Fonts->GetGlyphRangesKorean();
        cjkFonts = {"C:/Windows/Fonts/malgun.ttf"};
    } else if (lang == "zh") {
        cjkRanges = io.Fonts->GetGlyphRangesChineseSimplifiedCommon();
        cjkFonts = {"C:/Windows/Fonts/msyh.ttc", "C:/Windows/Fonts/simsun.ttc"};
    }
    auto mergeCjk = [&]() {
        if (!cjkRanges) return;
        for (const char* f : cjkFonts) {
            std::error_code ec;
            if (!std::filesystem::exists(f, ec)) continue;
            ImFontConfig cfg;
            cfg.MergeMode = true;
            io.Fonts->AddFontFromFileTTF(f, 15.0f, &cfg, cjkRanges);
            break;
        }
    };

    for (const char* f : regular) {
        std::error_code ec;
        if (std::filesystem::exists(f, ec) && io.Fonts->AddFontFromFileTTF(f, 15.0f, nullptr, ranges.Data)) break;
    }
    mergeCjk();
    for (const char* f : bold) {
        std::error_code ec;
        if (std::filesystem::exists(f, ec)) {
            g_fontBold = io.Fonts->AddFontFromFileTTF(f, 15.0f, nullptr, ranges.Data);
            break;
        }
    }
    mergeCjk();
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
    icons::setIconStyle(preset.layout.arrangement == Arrangement::Blender ? icons::IconStyle::Flat
                                                                          : icons::IconStyle::Color);
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
    const char* title = g.optWin == UiState::OptWin::RectSel   ? tr("Selección Rectangular###toolopts")
                        : g.optWin == UiState::OptWin::LassoSel ? tr("Selección con Lazo###toolopts")
                        : g.optWin == UiState::OptWin::Optimize ? tr("Optimizar###toolopts")
                                                                : tr("Conectar Objetos###toolopts");
    ImGui::SetNextWindowPos(g.optPos, ImGuiCond_Appearing);
    bool open = true;
    if (ImGui::Begin(title, &open, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse)) {
        if (g.optWin == UiState::OptWin::Optimize) {
            ImGui::SetNextItemWidth(120.0f);
            ImGui::InputFloat(tr("Tolerancia (cm)"), &g.optimizeTolCm, 0.01f, 0.1f, "%.3f");
            g.optimizeTolCm = std::clamp(g.optimizeTolCm, 0.0f, 1000.0f);
            ImGui::TextDisabled(tr("Une los puntos más cercanos que la tolerancia y quita\nlos puntos sueltos y los polígonos sin área."));
        } else if (g.optWin == UiState::OptWin::Connect) {
            ImGui::Checkbox(tr("Soldar"), &g.connectWeld);
            ImGui::BeginDisabled(!g.connectWeld);
            ImGui::SetNextItemWidth(120.0f);
            ImGui::InputFloat(tr("Tolerancia (cm)"), &g.connectWeldCm, 0.01f, 0.1f, "%.3f");
            g.connectWeldCm = std::clamp(g.connectWeldCm, 0.0f, 1000.0f);
            ImGui::EndDisabled();
            ImGui::TextDisabled(tr("Une los puntos que coinciden (misma posición,\nnormal y color) al conectar."));
        } else {
            ImGui::Checkbox(tr("Tolerante"), &g.selTolerant);
            ImGui::TextDisabled(g.selTolerant ? tr("Se elige todo lo que toque la zona.")
                                              : tr("Solo se elige lo que queda dentro entero."));
            ImGui::Checkbox(tr("Solo Seleccionar Visibles"), &g.selVisibleOnly);
            ImGui::TextDisabled(g.selVisibleOnly ? tr("Puntos, aristas y polígonos tapados por la malla no se eligen.")
                                                 : tr("También se eligen los tapados (a través de la malla)."));
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
    glm::vec3 bgTop(rs.bgTop[0], rs.bgTop[1], rs.bgTop[2]), bgBottom(rs.bgBottom[0], rs.bgBottom[1], rs.bgBottom[2]);
    {
        std::function<const SceneNode*(const SceneNode&)> findEnv = [&](const SceneNode& n) -> const SceneNode* {
            if (n.env.present) return &n;
            for (const auto& c : n.children())
                if (const SceneNode* f = findEnv(*c)) return f;
            return nullptr;
        };
        if (const SceneNode* en = findEnv(root)) {  // un objeto Fondo o Cielo manda sobre los ajustes de render
            bgTop = en->env.top;
            bgBottom = en->env.bottom;
        }
    }
    RenderFx fx;
    fx.studio = rs.studio;
    fx.shadow = rs.shadow;
    Thumbnail img = renderCameraImage(root, materials, camera.viewMatrix(), proj, w, h, rs.antialias, bgTop, bgBottom, fx);
    auto& pv = g.pv;
    pv.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    pv.aa = rs.antialias;
    pv.viewName = viewLabel(camera.view(), tr("Perspectiva"));
    freeUiTexture(pv.tex);
    pv.image = std::move(img);
    pv.tex = makeUiTexture(pv.image.rgba.data(), pv.image.width, pv.image.height);
    g.showPictureViewer = true;
    char buf[160];
    std::snprintf(buf, sizeof(buf), tr("Render: %d x %d en %.2f s"), pv.image.width, pv.image.height, pv.seconds);
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
    if (!ImGui::Begin(tr("Visor de Imágenes"), &g.showPictureViewer, ImGuiWindowFlags_MenuBar)) {
        ImGui::End();
        return;
    }
    if (ImGui::BeginMenuBar()) {
        if (ImGui::BeginMenu(tr("Archivo"))) {
            if (ImGui::MenuItem(tr("Guardar como..."), nullptr, false, !pv.image.rgba.empty())) {
                std::time_t now = std::time(nullptr);
                std::tm tmNow{};
#ifdef _WIN32
                localtime_s(&tmNow, &now);
#else
                localtime_r(&now, &tmNow);
#endif
                char name[64];
                std::strftime(name, sizeof(name), tr("render_%Y%m%d_%H%M%S.png"), &tmNow);
                if (auto path = saveFileDialog(tr("Guardar render"), {{tr("Imagen PNG"), "*.png"}}, name, "png")) {
                    flashStatus(savePng(*path, pv.image) ? tr("Render guardado: ") + path->filename().string()
                                                         : std::string(tr("No se pudo guardar el render.")));
                }
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu(tr("Ver"))) {
            if (ImGui::MenuItem(tr("Ajustar a la Ventana"), nullptr, pv.zoom == 0.0f)) pv.zoom = 0.0f;
            if (ImGui::MenuItem("50 %", nullptr, pv.zoom == 0.5f)) pv.zoom = 0.5f;
            if (ImGui::MenuItem("100 %", nullptr, pv.zoom == 1.0f)) pv.zoom = 1.0f;
            if (ImGui::MenuItem("200 %", nullptr, pv.zoom == 2.0f)) pv.zoom = 2.0f;
            ImGui::EndMenu();
        }
        ImGui::EndMenuBar();
    }
    if (ImGui::Button(tr("Renderizar"))) renderToPictureViewer(root, materials, camera, vp);
    ImGui::SameLine();
    if (ImGui::Button(tr("Configuración..."))) g.showRenderSettings = true;
    ImGui::SameLine();
    if (pv.image.rgba.empty()) ImGui::TextDisabled(tr("Todavía no hay ningún render (Mayús+R)."));
    else
        ImGui::TextDisabled(tr("%s · %d x %d · antialiasing %dx%d · %.2f s · %d triángulos"), pv.viewName.c_str(),
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

// Modo Textura: panel con la etiqueta de Textura (proyeccion, desplazamiento y longitud) del objeto seleccionado.
// No hay manipulador en el visor; solo estos campos.
void drawTextureMode(SceneNode& root, SceneNode* selected, SceneHistory& history, ViewportState& vp, const std::vector<Material>& materials) {
    if (!vp.texMode) return;
    ImGui::SetNextWindowSize(ImVec2(360, 0), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(tr("Modo Textura"), &vp.texMode, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::End();
        return;
    }
    if (!selected || !hasGeometry(selected->primitive)) {
        ImGui::TextUnformatted(tr("Seleccione un objeto con geometría."));
        ImGui::End();
        return;
    }
    TextureTagData& t = selected->texTag;
    ImGui::Text("%s: %s", tr("Objeto"), selected->name().c_str());
    ImGui::Text("%s: %s", tr("Material"),
                (selected->materialIndex >= 0 && (size_t)selected->materialIndex < materials.size())
                    ? materials[(size_t)selected->materialIndex].name.c_str()
                    : tr("(ninguno)"));
    struct Proj { int id; const char* name; };
    const Proj projs[] = {{6, tr("UVW")}, {0, tr("Esférico")}, {1, tr("Cilíndrico")}, {2, tr("Plano")}, {3, tr("Cúbico")}, {4, tr("Frontal")}};
    const char* cur = tr("Otra");
    for (const Proj& p : projs) if (p.id == t.projection) cur = p.name;
    if (ImGui::BeginCombo(tr("Proyección"), cur)) {
        for (const Proj& p : projs)
            if (ImGui::Selectable(p.name, p.id == t.projection)) { history.commitBefore(root); t.projection = p.id; }
        ImGui::EndCombo();
    }
    bool changed = false;
    changed |= ImGui::DragFloat2(tr("Desplazamiento U/V"), &t.offset.x, 0.005f, -100.0f, 100.0f, "%.3f");
    if (ImGui::IsItemActivated()) history.commitBefore(root);  // un paso de deshacer por arrastre
    changed |= ImGui::DragFloat2(tr("Longitud U/V"), &t.length.x, 0.005f, 0.001f, 1000.0f, "%.3f");
    if (ImGui::IsItemActivated()) history.commitBefore(root);
    if (changed) { t.length.x = std::max(t.length.x, 0.001f); t.length.y = std::max(t.length.y, 0.001f); }
    if (t.projection != 6) ImGui::TextWrapped("%s", tr("Aviso: el visor y el render solo dibujan texturas con proyección UVW."));
    ImGui::Checkbox(tr("Mosaico"), &t.tile);
    if (ImGui::Button(tr("Restablecer"))) { history.commitBefore(root); t.offset = glm::vec2(0.0f); t.length = glm::vec2(1.0f); }
    ImGui::End();
}

void drawRenderSettings() {
    if (!g.showRenderSettings) return;
    auto& rs = g.render;
    ImGui::SetNextWindowSize(ImVec2(440, 400), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(tr("Configuración de Render"), &g.showRenderSettings)) {
        ImGui::End();
        return;
    }
    ImGui::SeparatorText(tr("Salida"));
    struct Preset { Tr name; int w, h; };
    static const Preset kPresets[] = {{"800 x 600", 800, 600},   {tr("640 x 480 (VGA)"), 640, 480},
                                      {"1280 x 720 (HD 720)", 1280, 720}, {tr("1920 x 1080 (HD 1080)"), 1920, 1080},
                                      {"3840 x 2160 (UHD 4K)", 3840, 2160}, {tr("1080 x 1080 (cuadrado)"), 1080, 1080}};
    if (ImGui::BeginCombo(tr("Preestablecido"), tr("Elegir..."))) {
        for (const Preset& p : kPresets)
            if (ImGui::Selectable(p.name)) { rs.width = p.w; rs.height = p.h; rs.useViewportSize = false; }
        ImGui::EndCombo();
    }
    ImGui::Checkbox(tr("Usar el tamaño del visor"), &rs.useViewportSize);
    ImGui::BeginDisabled(rs.useViewportSize);
    ImGui::InputInt(tr("Anchura (px)"), &rs.width, 10, 100);
    ImGui::InputInt(tr("Altura (px)"), &rs.height, 10, 100);
    rs.width = std::clamp(rs.width, 16, 8192);
    rs.height = std::clamp(rs.height, 16, 8192);
    ImGui::EndDisabled();
    ImGui::SeparatorText(tr("Antialiasing"));
    const char* aaNames[] = {tr("Ninguno (1x1)"), tr("Geometría (2x2)"), "3x3", tr("Mejor (4x4)")};
    int aaIdx = std::clamp(rs.antialias, 1, 4) - 1;
    if (ImGui::Combo(tr("Antialiasing"), &aaIdx, aaNames, 4)) rs.antialias = aaIdx + 1;
    ImGui::SeparatorText(tr("Efectos"));
    ImGui::Checkbox(tr("Reflejos de estudio y transparencia"), &rs.studio);
    ImGui::Checkbox(tr("Sombra suave en el suelo"), &rs.shadow);
    ImGui::SeparatorText(tr("Fondo"));
    ImGui::ColorEdit3(tr("Arriba"), rs.bgTop);
    ImGui::ColorEdit3(tr("Abajo"), rs.bgBottom);
    ImGui::Spacing();
    ImGui::TextDisabled(tr("Render por CPU con el sombreado de las miniaturas:\nsin sombras ni texturas todavía."));
    if (ImGui::Button(tr("Renderizar en Visor de Imágenes"))) g.renderRequested = true;
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
    if (c.visMesh != c.mesh.get() || c.visHidden != n.hiddenPolys.get() || c.visMvp != mvp || c.visW != (int)vp.w || c.visH != (int)vp.h) {
        c.vis = computeComponentVisibility(*c.mesh, c.topo, mvp, (int)vp.w, (int)vp.h,
                                           n.hasHidden() ? n.hiddenPolys.get() : nullptr);
        c.visMesh = c.mesh.get();
        c.visHidden = n.hiddenPolys.get();
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
        dl->AddText(ImVec2(vp.x + 8, vp.y + 24), IM_COL32(255, 200, 120, 255), tr("Malla demasiado grande para dibujar sus componentes."));
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
            // Un poligono convexo se rellena de una vez: rellenar sus triangulos por separado deja una
            // costura (la diagonal) por el suavizado de los bordes.
            {
                const auto& pl = t.polys[(size_t)i];
                std::vector<ImVec2> poly;
                bool allOk = pl.size() >= 3 && pl.size() <= 16;
                for (size_t k = 0; allOk && k < pl.size(); ++k) {
                    allOk = ok[(size_t)pl[k]] != 0;
                    if (allOk) poly.push_back(sp[(size_t)pl[k]]);
                }
                if (allOk) {
                    float area = 0.0f;
                    int pos = 0, neg = 0;
                    const size_t n = poly.size();
                    for (size_t k = 0; k < n; ++k) {
                        const ImVec2 &a = poly[k], &b = poly[(k + 1) % n], &c2 = poly[(k + 2) % n];
                        area += a.x * b.y - b.x * a.y;
                        const float cr = (b.x - a.x) * (c2.y - b.y) - (b.y - a.y) * (c2.x - b.x);
                        if (cr > 1e-3f) ++pos;
                        else if (cr < -1e-3f) ++neg;
                    }
                    if (pos == 0 || neg == 0) {  // convexo (o degenerado): un solo relleno
                        if (area < 0.0f) std::reverse(poly.begin(), poly.end());
                        dl->AddConvexPolyFilled(poly.data(), (int)poly.size(), IM_COL32(255, 140, 30, 90));
                        continue;
                    }
                }
            }
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
    const char* names[] = {"", tr("Puntos"), tr("Aristas"), tr("Polígonos")};
    char buf[128];
    std::snprintf(buf, sizeof(buf), tr("%s: %zu de %zu seleccionados"), names[c.mode], c.sel.size(), componentCount());
    dl->AddText(ImVec2(vp.x + 8, vp.y + 24), IM_COL32(230, 230, 230, 220), buf);
    dl->PopClipRect();
}

int componentHit(SceneNode* selected, const OrbitalCamera& camera, float mx, float my) {
    if (!g_vp || !syncComponents(selected)) return -2;
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
    return best;
}

bool componentSelected(int index) { return g.comp.sel.count(index) > 0; }

void clearComponentSelection() { g.comp.sel.clear(); }

bool componentClick(SceneNode* selected, const OrbitalCamera& camera, float mx, float my, bool shift, bool ctrl) {
    const int best = componentHit(selected, camera, mx, my);
    if (best == -2) return false;
    auto& c = g.comp;
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
int componentModeFor(const SceneNode* n) { return syncComponents(n) ? g.comp.mode : 0; }

// Operaciones de los menus Seleccion y Malla sobre los componentes. Las de
// seleccion no crean paso de deshacer (como el resto de la seleccion de
// componentes); las de malla si.
bool runCompOp(SceneNode& root, SceneNode* selected, SceneHistory& history, const std::string& op) {
    if (op == "modelsettings") {  // Configuracion Modelado: los ajustes de modelado viven en Preferencias
        g.showPrefs = true;
        return false;
    }
    if (!syncComponents(selected)) {
        flashStatus(tr("Selecciona un objeto editable y entra en modo Puntos, Aristas o Polígonos."));
        return false;
    }
    auto& c = g.comp;
    const MeshTopology& t = c.topo;
    const int mode = c.mode;
    auto done = [&](const char* what) {
        flashStatus(std::string(what) + ": " + std::to_string(c.sel.size()) + tr(" seleccionados."));
        return true;
    };
    if (op == "loop" || op == "ring") {
        if (mode != 2) { flashStatus(tr("Bucle y Anillo funcionan en modo Aristas.")); return false; }
        std::set<int> out;
        for (int e : c.sel) {
            const std::set<int> part = op == "loop" ? edgeLoop(t, e) : edgeRing(t, e);
            out.insert(part.begin(), part.end());
        }
        c.sel = out;
        return done(op == "loop" ? tr("Selección de Bucle") : tr("Selección de Anillo"));
    }
    if (op == "pattern2" || op == "pattern2b" || op == "pattern3") {
        const int step = op == "pattern3" ? 3 : 2;
        c.sel = patternSelection(c.sel, step, op == "pattern2b" ? 1 : 0);
        return done(tr("Seleccionar Patrón"));
    }
    if (op == "grow") { c.sel = growSelection(t, mode, c.sel); return done(tr("Aumentar Selección")); }
    if (op == "shrink") { c.sel = shrinkSelection(t, mode, c.sel); return done(tr("Reducir Selección")); }
    if (op == "hide" || op == "hideinv" || op == "unhide") {
        if (op == "unhide") {
            selected->hiddenPolys.reset();
            selected->hiddenFor = nullptr;
            flashStatus(tr("Mostrar Todo"));
            return true;
        }
        std::vector<char> hid(t.polys.size(), 0);
        if (selected->hasHidden())
            for (size_t i = 0; i < hid.size() && i < selected->hiddenPolys->size(); ++i) hid[i] = (*selected->hiddenPolys)[i];
        std::vector<char> inSel(t.polys.size(), 0);
        if (mode == 3) {
            for (int i : c.sel) if (i >= 0 && (size_t)i < inSel.size()) inSel[(size_t)i] = 1;
        } else {  // puntos/aristas: poligonos cuyos puntos estan todos elegidos
            const std::set<int> pts = selectedPoints();
            for (size_t p = 0; p < t.polys.size(); ++p) {
                bool all = !t.polys[p].empty();
                for (int q : t.polys[p]) all = all && pts.count(q) > 0;
                inSel[p] = all ? 1 : 0;
            }
        }
        for (size_t p = 0; p < hid.size(); ++p)
            if (op == "hide" ? inSel[p] : !inSel[p]) hid[p] = 1;
        size_t shown = 0;
        for (char h : hid) shown += h ? 0 : 1;
        if (shown == 0) { flashStatus(tr("No se puede ocultar todo el objeto.")); return false; }
        selected->hiddenPolys = std::make_shared<const std::vector<char>>(std::move(hid));
        selected->hiddenFor = selected->mesh.get();
        c.sel.clear();
        flashStatus(op == "hide" ? tr("Ocultar Selección") : tr("Ocultar No Seleccionados"));
        return true;
    }
    if (op == "outline") { c.sel = outlineSelection(t, mode, c.sel); return done(tr("Selección Contorno")); }
    if (op == "fill") { c.sel = fillSelection(t, c.sel); return done(tr("Rellenar Selección")); }
    if (op == "connected") { c.sel = connectedSelection(t, mode, c.sel); return done(tr("Seleccionar Conectados")); }
    if (op == "invert") {
        std::set<int> out;
        for (int i = 0; i < (int)componentCount(); ++i)
            if (!c.sel.count(i)) out.insert(i);
        c.sel = out;
        return done(tr("Invertir Selección"));
    }

    if (op == "resetscale") {
        // Restaurar Escala: la escala del objeto pasa a los puntos y el objeto queda a escala 1 (misma forma en pantalla).
        if (!selected || !selected->mesh) return false;
        if (!selected->children().empty()) { flashStatus(tr("Restaurar Escala: el objeto tiene hijos; sus posiciones cambiarían.")); return false; }
        if (selected->scale == glm::vec3(1.0f)) { flashStatus(tr("Restaurar Escala: el objeto ya está a escala 1.")); return false; }
        history.commitBefore(root);
        MeshData m = *selected->mesh;
        const glm::vec3 sc = selected->scale;
        for (Vertex& v : m.vertices) {
            v.position *= sc;
            const glm::vec3 nn = v.normal / sc;  // normal bajo escala no uniforme: inversa traspuesta
            if (glm::length(nn) > 1e-12f) v.normal = glm::normalize(nn);
        }
        updateBounds(m);
        selected->mesh = std::make_shared<const MeshData>(std::move(m));
        selected->scale = glm::vec3(1.0f);
        syncComponents(selected);
        flashStatus(tr("Restaurar Escala: hecho."));
        return true;
    }
    if (op == "check") {
        const MeshCheck chk = checkMesh(t);
        if (chk.clean()) { flashStatus(tr("Comprobar Malla: sin problemas.")); return true; }
        // Selecciona lo malo del modo actual: puntos sueltos, aristas abiertas/no manifold, poligonos sin area.
        if (mode == 1) c.sel = chk.loosePointSet;
        else if (mode == 2) { c.sel = chk.openEdgeSet; c.sel.insert(chk.nonManifoldEdgeSet.begin(), chk.nonManifoldEdgeSet.end()); }
        else c.sel = chk.degeneratePolySet;
        flashStatus(tr("Comprobar Malla: ") + std::to_string(chk.openEdges) + tr(" aristas abiertas, ") +
                    std::to_string(chk.nonManifoldEdges) + tr(" no manifold, ") + std::to_string(chk.loosePoints) +
                    tr(" puntos sueltos, ") + std::to_string(chk.degeneratePolys) + tr(" polígonos sin área."));
        return true;
    }

    MeshEditResult r;
    const char* name = "";
    std::unique_ptr<MeshData> splitPart;
    if (op == "bridge") {
        name = tr("Puente");
        if (mode != 3 || c.sel.size() != 2) { flashStatus(tr("Puente: selecciona exactamente 2 polígonos (modo Polígonos).")); return false; }
        r = bridgePolygons(*c.mesh, t, *c.sel.begin(), *c.sel.rbegin());
        if (!r.changed) { flashStatus(tr("Puente: los dos polígonos deben tener el mismo número de lados.")); return false; }
    } else if (op == "weld") {
        name = tr("Soldar");
        r = weldPoints(*c.mesh, t, selectedPoints());
        if (!r.changed) { flashStatus(tr("Soldar: selecciona al menos 2 puntos.")); return false; }
    } else if (op == "close") {
        name = tr("Cerrar Agujero Poligonal");
        r = closeHoles(*c.mesh, t, selectedPoints());
        if (!r.changed) { flashStatus(tr("Cerrar Agujero: no hay bordes abiertos que toquen la selección.")); return false; }
    } else if (op == "subdiv" || op == "flip" || op == "align" || op == "triang" || op == "untriang") {
        name = op == "subdiv" ? tr("Subdividir") : op == "flip" ? tr("Invertir Normales") : op == "align" ? tr("Alinear Normales") : op == "untriang" ? tr("Destriangular") : tr("Triangular");
        std::set<int> polys;
        if (mode == 3) polys = c.sel;
        if (polys.empty())
            for (int p = 0; p < (int)t.polys.size(); ++p) polys.insert(p);
        if (op == "subdiv") r = subdividePolygons(*c.mesh, t, polys);
        else if (op == "flip") r = flipPolygons(*c.mesh, t, polys);
        else if (op == "align") {
            r = alignNormals(*c.mesh, t, polys);
            if (!r.changed) { flashStatus(tr("Alinear Normales: ya están todas alineadas.")); return false; }
        } else if (op == "untriang") {
            r = untriangulatePolygons(*c.mesh, t, mode == 3 ? c.sel : std::set<int>{});
            if (!r.changed) { flashStatus(tr("Destriangular: no hay triángulos vecinos que unir.")); return false; }
        } else {
            r = triangulatePolygons(*c.mesh, t, polys);
            if (!r.changed) { flashStatus(tr("Triangular: no hay polígonos de más de 3 lados.")); return false; }
        }
    } else if (op == "optimize") {
        name = tr("Optimizar");
        r = optimizeMesh(*c.mesh, t, g.optimizeTolCm / kCmPerUnit);
        if (!r.changed) { flashStatus(tr("Optimizar: la malla ya está limpia.")); return false; }
    } else if (op == "collapse") {
        name = tr("Colapso");
        if (mode == 3) {
            if (c.sel.empty()) { flashStatus(tr("Colapso: selecciona polígonos.")); return false; }
            r = collapsePolygons(*c.mesh, t, c.sel);
        } else {
            r = weldPoints(*c.mesh, t, selectedPoints());
            if (!r.changed) { flashStatus(tr("Colapso: selecciona al menos 2 puntos.")); return false; }
        }
    } else if (op == "relax") {
        name = tr("Planchar");
        r = relaxPoints(*c.mesh, t, selectedPoints(), 0.5f, 3);
        if (!r.changed) { flashStatus(tr("Planchar: no hay puntos que alisar.")); return false; }
    } else if (op == "turn") {
        name = tr("Voltear Arista");
        std::set<std::pair<int, int>> ed;
        if (mode == 2) for (int i : c.sel) if (i >= 0 && (size_t)i < t.edges.size()) ed.insert(t.edges[(size_t)i]);
        if (ed.empty()) { flashStatus(tr("Voltear Arista: selecciona aristas (modo Aristas).")); return false; }
        r = turnEdges(*c.mesh, t, ed);
        if (!r.changed) { flashStatus(tr("Voltear Arista: solo entre dos triángulos y sin cuadrilátero cóncavo.")); return false; }
    } else if (op == "mirror") {
        name = tr("Reflejar");
        r = mirrorPolygons(*c.mesh, t, mode == 3 ? c.sel : std::set<int>{}, g.symAxis >= 0 ? g.symAxis : 0);
        if (!r.changed) { flashStatus(tr("Reflejar: no hay polígonos.")); return false; }
    } else if (op == "normalmove" || op == "normalscale" || op == "normalrot") {
        // Valores fijos (como Planchar): 10 cm, x1,25 y 15 grados; se pueden repetir o deshacer.
        const int kind = op == "normalmove" ? 0 : op == "normalscale" ? 1 : 2;
        name = kind == 0 ? tr("Mover Normal") : kind == 1 ? tr("Escalar Normal") : tr("Rotar Normal");
        const std::set<int> pts = selectedPoints();
        if (pts.empty()) { flashStatus(std::string(name) + tr(": no hay nada seleccionado.")); return false; }
        r = transformAlongNormal(*c.mesh, t, pts, kind, kind == 0 ? 10.0f / kCmPerUnit : kind == 1 ? 1.25f : 15.0f);
        if (!r.changed) { flashStatus(std::string(name) + tr(": la selección no tiene normal definida.")); return false; }
    } else if (op == "array") {
        name = tr("Matriz");
        const std::set<int> ps = mode == 3 ? c.sel : std::set<int>{};
        glm::vec3 lo(1e30f), hi(-1e30f);
        for (size_t p = 0; p < t.polys.size(); ++p) {
            if (!ps.empty() && !ps.count((int)p)) continue;
            for (int q : t.polys[p]) {
                lo = glm::min(lo, t.points[(size_t)q]);
                hi = glm::max(hi, t.points[(size_t)q]);
            }
        }
        if (lo.x > hi.x) { flashStatus(tr("Matriz: no hay polígonos.")); return false; }
        // 3 copias desplazadas en X el ancho de la seleccion mas un 10 %.
        r = arrayPolygons(*c.mesh, t, ps, 3, glm::vec3((hi.x - lo.x) * 1.1f, 0.0f, 0.0f));
        if (!r.changed) { flashStatus(tr("Matriz: no hay polígonos.")); return false; }
    } else if (op == "clone") {
        name = tr("Clonar");
        if (mode != 3 || c.sel.empty()) { flashStatus(tr("Clonar: selecciona polígonos (modo Polígonos).")); return false; }
        r = clonePolygons(*c.mesh, t, c.sel);
    } else if (op == "connect") {
        name = tr("Conectar");
        r = connectPoints(*c.mesh, t, selectedPoints());
        if (!r.changed) { flashStatus(tr("Conectar: selecciona 2 puntos no vecinos de un mismo polígono.")); return false; }
    } else if (op == "dissolve" || op == "melt") {
        name = op == "melt" ? tr("Fundir") : tr("Disolver");
        std::set<std::pair<int, int>> ed;
        if (mode == 1) {
            // Fundir/Disolver puntos: se une todo lo que rodea al punto (todas sus aristas).
            const std::set<int> pts = selectedPoints();
            for (const auto& e : t.edges) if (pts.count(e.first) || pts.count(e.second)) ed.insert(e);
        } else if (mode == 2) {
            for (int i : c.sel) if (i >= 0 && (size_t)i < t.edges.size()) ed.insert(t.edges[(size_t)i]);
        } else if (mode == 3) {
            // Aristas compartidas por dos poligonos seleccionados.
            std::map<std::pair<int, int>, int> cnt;
            for (int p : c.sel) {
                if (p < 0 || (size_t)p >= t.polys.size()) continue;
                const auto& pl = t.polys[(size_t)p];
                for (size_t k = 0; k < pl.size(); ++k) {
                    const int a = pl[k], b = pl[(k + 1) % pl.size()];
                    ++cnt[{std::min(a, b), std::max(a, b)}];
                }
            }
            for (const auto& [e, n] : cnt) if (n >= 2) ed.insert(e);
        }
        if (ed.empty()) { flashStatus(tr("Disolver: selecciona aristas, o polígonos vecinos.")); return false; }
        r = dissolveEdges(*c.mesh, t, ed);
        if (!r.changed) { flashStatus(tr("Disolver: el resultado no sería un polígono convexo.")); return false; }
    } else if (op == "rmngons" || op == "retringons") {
        std::set<int> ng;
        for (int p = 0; p < (int)t.polys.size(); ++p)
            if (t.polys[(size_t)p].size() > 4) ng.insert(p);
        if (ng.empty()) { flashStatus(tr("No hay N-gons (polígonos de más de 4 lados).")); return false; }
        if (op == "retringons") {
            name = tr("Retriangular N-gons");
            r = triangulatePolygons(*c.mesh, t, ng);
        } else {
            name = tr("Eliminar N-gons");
            std::vector<char> keep(t.polys.size(), 1);
            for (int p : ng) keep[(size_t)p] = 0;
            r.mesh = keepPolygons(*c.mesh, t, keep);
            r.changed = (int)ng.size();
            if (r.mesh.indices.empty()) { flashStatus(tr("Eliminar N-gons: no quedaría nada de la malla.")); return false; }
        }
    } else if (op == "split") {
        name = tr("Separar");
        if (mode != 3 || c.sel.empty()) { flashStatus(tr("Separar: selecciona polígonos (modo Polígonos).")); return false; }
        if (c.sel.size() >= t.polys.size()) { flashStatus(tr("Separar: queda toda la malla seleccionada.")); return false; }
        std::vector<char> inPart(t.polys.size(), 0), inRest(t.polys.size(), 1);
        for (int p : c.sel) if (p >= 0 && (size_t)p < inPart.size()) { inPart[(size_t)p] = 1; inRest[(size_t)p] = 0; }
        splitPart = std::make_unique<MeshData>(keepPolygons(*c.mesh, t, inPart));
        r.mesh = keepPolygons(*c.mesh, t, inRest);
        r.changed = (int)c.sel.size();
    } else {
        flashStatus(tr("Operación desconocida: ") + op);
        return false;
    }
    if (!r.changed) return false;
    history.commitBefore(root);
    if (splitPart && selected->parent()) {
        // El trozo separado es un objeto nuevo, hermano del original y con su misma transformacion y material.
        auto made = std::make_unique<SceneNode>(selected->name() + tr(" (separado)"), PrimitiveType::Mesh);
        made->mesh = std::make_shared<const MeshData>(std::move(*splitPart));
        made->materialIndex = selected->materialIndex;
        made->position = selected->position;
        made->rotationDeg = selected->rotationDeg;
        made->scale = selected->scale;
        SceneNode* parentNode = selected->parent();
        parentNode->insertChild(std::move(made), parentNode->indexOf(selected) + 1);
    }
    const std::vector<int> selTris = r.selectTris;
    selected->mesh = std::make_shared<const MeshData>(std::move(r.mesh));
    if (!selTris.empty() && g_vp) g_vp->compMode = 3;
    if (syncComponents(selected)) c.sel = selTris.empty() ? std::set<int>{} : polysFromTris(c.topo, selTris);
    flashStatus(std::string(name) + ": " + std::to_string(r.changed) + (r.changed == 1 ? tr(" elemento.") : tr(" elementos.")));
    return true;
}

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
    flashStatus(tr("Eliminados ") + std::to_string(removed) + (removed == 1 ? tr(" polígono.") : tr(" polígonos.")));
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
    // Posicion nueva por punto de la topologia: los seleccionados siguen el gizmo; con la simetria activa,
    // su espejo sin seleccionar copia la posicion reflejada y los puntos sobre el plano se quedan en el.
    const size_t np = c.topo.points.size();
    std::vector<glm::vec3> newPos(np, glm::vec3(0.0f)), startPos(np, glm::vec3(0.0f));
    std::vector<char> has(np, 0);
    for (size_t v = 0; v < m.vertices.size(); ++v) {
        const int q = c.topo.vertexPoint[v];
        if (q >= 0 && (size_t)q < np) startPos[(size_t)q] = m.vertices[v].position;
    }
    for (int q : pts) {
        if (q < 0 || (size_t)q >= np) continue;
        newPos[(size_t)q] = glm::vec3(l * glm::vec4(startPos[(size_t)q], 1.0f));
        has[(size_t)q] = 1;
    }
    if (g.symAxis >= 0 && g.symAxis < 3) {
        const int ax = g.symAxis;
        const float tol = std::max(1e-5f, g.symTolCm / kCmPerUnit);
        const float cell = tol * 2.0f;
        auto key = [&](const glm::vec3& p) {
            return std::array<int, 3>{(int)std::floor(p.x / cell), (int)std::floor(p.y / cell), (int)std::floor(p.z / cell)};
        };
        std::map<std::array<int, 3>, std::vector<int>> grid;
        for (size_t q = 0; q < np; ++q) grid[key(startPos[q])].push_back((int)q);
        auto mirror = [&](glm::vec3 p) { p[ax] = -p[ax]; return p; };
        std::vector<std::pair<int, int>> pairs;  // (seleccionado, espejo)
        for (int q : pts) {
            if (q < 0 || (size_t)q >= np) continue;
            const glm::vec3 target = mirror(startPos[(size_t)q]);
            const auto k = key(target);
            int best = -1;
            float bestD = tol;
            for (int dx = -1; dx <= 1; ++dx)
                for (int dy = -1; dy <= 1; ++dy)
                    for (int dz = -1; dz <= 1; ++dz) {
                        const auto it = grid.find({k[0] + dx, k[1] + dy, k[2] + dz});
                        if (it == grid.end()) continue;
                        for (int o : it->second) {
                            const float d = glm::length(startPos[(size_t)o] - target);
                            if (d <= bestD) { bestD = d; best = o; }
                        }
                    }
            if (best >= 0) pairs.push_back({q, best});
        }
        for (const auto& pr : pairs) {
            if (pr.second == pr.first) {  // sobre el plano: se queda en el plano
                newPos[(size_t)pr.first][ax] = 0.0f;
            } else if (!pts.count(pr.second)) {
                newPos[(size_t)pr.second] = mirror(newPos[(size_t)pr.first]);
                has[(size_t)pr.second] = 1;
            }
        }
    }
    for (size_t v = 0; v < m.vertices.size(); ++v) {
        const int q = c.topo.vertexPoint[v];
        if (q >= 0 && (size_t)q < np && has[(size_t)q]) {
            m.vertices[v].position = newPos[(size_t)q];
            moved[v] = 1;
        }
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

// --- Herramientas de modelado --------------------------------------------------------
bool modelToolActive() { return g.modelTool != UiState::ModelTool::None; }

// Aplica la herramienta con un valor (cm) a la seleccion de partida y deja
// seleccionado el resultado. `start` y su topologia no se tocan.
bool applyPolyTool(SceneNode& node, const std::shared_ptr<const MeshData>& start, const MeshTopology& topo,
                   const std::set<int>& sel, float a, float b) {
    MeshEditResult r;
    switch (g.modelTool) {
        case UiState::ModelTool::Extrude: r = extrudePolygons(*start, topo, sel, a / kCmPerUnit); break;
        case UiState::ModelTool::InnerExtrude: r = bevelPolygons(*start, topo, sel, a / kCmPerUnit, 0.0f); break;
        case UiState::ModelTool::Bevel: r = bevelPolygons(*start, topo, sel, a / kCmPerUnit, b / kCmPerUnit); break;
        default: return false;
    }
    if (!r.changed) return false;
    const std::vector<int> selTris = r.selectTris;
    node.mesh = std::make_shared<const MeshData>(std::move(r.mesh));
    if (syncComponents(&node)) g.comp.sel = polysFromTris(g.comp.topo, selTris);
    return true;
}

// Cantidad principal de la herramienta (la que cambia al arrastrar).
float& toolValue() {
    switch (g.modelTool) {
        case UiState::ModelTool::InnerExtrude: return g.innerInsetCm;
        case UiState::ModelTool::Bevel: return g.bevelInsetCm;
        default: return g.extrudeCm;
    }
}

// Aplicar con los valores de la ventana sobre la seleccion actual.
bool applyToolNow(SceneNode& root, SceneNode* selected, SceneHistory& history) {
    if (!syncComponents(selected) || g.comp.mode != 3 || g.comp.sel.empty()) {
        flashStatus(std::string(modelToolName(g.modelTool)) + tr(": selecciona polígonos de un objeto editable."));
        return false;
    }
    const std::shared_ptr<const MeshData> start = selected->mesh;
    const MeshTopology topo = g.comp.topo;
    const std::set<int> sel = g.comp.sel;
    history.commitBefore(root);
    const float b = g.bevelExtrudeCm;
    return applyPolyTool(*selected, start, topo, sel, toolValue(), b);
}

// Cuchillo en modo Linea: plano que pasa por la camara y el tramo dibujado.
// (sx0,sy0)-(sx1,sy1) en coordenadas de ventana.
int knifeLine(SceneNode& root, SceneNode* selected, const OrbitalCamera& camera, const ViewportState& vp,
              SceneHistory& history, ImVec2 s0, ImVec2 s1) {
    if (!editableMesh(selected)) {
        flashStatus(tr("Cuchillo: selecciona un objeto editable (C)."));
        return 0;
    }
    const float len2 = (s1.x - s0.x) * (s1.x - s0.x) + (s1.y - s0.y) * (s1.y - s0.y);
    if (len2 < 16.0f) return 0;
    const glm::mat4 view = camera.viewMatrix();
    const glm::mat4 proj = camera.projectionMatrix(vp.w / std::max(1.0f, vp.h), false);
    const glm::vec3 eye = glm::vec3(glm::inverse(view)[3]);
    // screenRay espera la proyeccion con la Y invertida (la de main.cpp); `proj` (sin invertir) es para proyectar a pantalla.
    const glm::mat4 projRay = camera.projectionMatrix(vp.w / std::max(1.0f, vp.h), true);
    const Ray r0 = TransformGizmo::screenRay(s0.x - vp.x, s0.y - vp.y, (int)vp.w, (int)vp.h, view, projRay, eye);
    const Ray r1 = TransformGizmo::screenRay(s1.x - vp.x, s1.y - vp.y, (int)vp.w, (int)vp.h, view, projRay, eye);
    const glm::vec3 between = r1.origin - r0.origin;
    // Perspectiva: los dos rayos salen del ojo; ortografica: son paralelos.
    glm::vec3 nW = glm::length(between) > 1e-6f ? glm::cross(r0.direction, between) : glm::cross(r0.direction, r1.direction);
    if (glm::length(nW) < 1e-12f) return 0;
    const glm::mat4 W = selected->worldMatrix();
    const glm::vec3 pL = glm::vec3(glm::inverse(W) * glm::vec4(r0.origin, 1.0f));
    const glm::vec3 nL = glm::transpose(glm::mat3(W)) * nW;
    const MeshTopology topo = buildMeshTopology(*selected->mesh);
    std::vector<char> allowed;
    const bool polySel = g_vp && g_vp->compMode == 3 && syncComponents(selected) && !g.comp.sel.empty();
    if (polySel) {
        allowed.assign(topo.polys.size(), 0);
        for (int p : g.comp.sel) if (p < (int)allowed.size()) allowed[(size_t)p] = 1;
    } else if (g.knifeVisibleOnly) {
        const glm::mat4 mvp = proj * view * W;
        allowed = computeComponentVisibility(*selected->mesh, topo, mvp, (int)vp.w, (int)vp.h).polys;
    }
    const CompProjector pr{proj * view * W, vp.x, vp.y, vp.w, vp.h};
    auto inRange = [&](const glm::vec3& local) {
        ImVec2 p;
        if (!pr(local, p)) return false;
        const float t = ((p.x - s0.x) * (s1.x - s0.x) + (p.y - s0.y) * (s1.y - s0.y)) / len2;
        return t >= -0.01f && t <= 1.01f;
    };
    MeshEditResult r = knifeCut(*selected->mesh, topo, pL, nL, allowed, inRange);
    if (!r.changed) {
        flashStatus(tr("Cuchillo: la línea no corta ningún polígono."));
        return 0;
    }
    history.commitBefore(root);
    selected->mesh = std::make_shared<const MeshData>(std::move(r.mesh));
    if (g_vp) g.comp.sel.clear();
    flashStatus(tr("Cuchillo: ") + std::to_string(r.changed) + (r.changed == 1 ? tr(" polígono cortado.") : tr(" polígonos cortados.")));
    return r.changed;
}

// Cuchillo en modo Bucle: el clic corta el anillo de la arista mas cercana (a menos de 14 px), por el punto
// donde esta el cursor a lo largo de esa arista.
int knifeLoopAt(SceneNode& root, SceneNode* selected, const OrbitalCamera& camera, const ViewportState& vp,
                SceneHistory& history, float mx, float my) {
    if (!editableMesh(selected)) {
        flashStatus(tr("Cuchillo: selecciona un objeto editable (C)."));
        return 0;
    }
    const MeshTopology topo = buildMeshTopology(*selected->mesh);
    const CompProjector pr = compProjector(*selected, camera, vp);
    std::vector<char> visible;
    if (g.knifeVisibleOnly) {
        const glm::mat4 mvp = pr.mvp;
        visible = computeComponentVisibility(*selected->mesh, topo, mvp, (int)vp.w, (int)vp.h).edges;
    }
    int best = -1;
    float bestD = 14.0f, bestT = 0.5f;
    for (size_t e = 0; e < topo.edges.size(); ++e) {
        if (!visible.empty() && (e >= visible.size() || !visible[e])) continue;
        ImVec2 a, b;
        if (!pr(topo.points[(size_t)topo.edges[e].first], a) || !pr(topo.points[(size_t)topo.edges[e].second], b)) continue;
        const float dx = b.x - a.x, dy = b.y - a.y, l2 = dx * dx + dy * dy;
        if (l2 < 1.0f) continue;
        const float t = std::clamp(((mx - a.x) * dx + (my - a.y) * dy) / l2, 0.0f, 1.0f);
        const float ex = a.x + dx * t - mx, ey = a.y + dy * t - my;
        const float d = std::sqrt(ex * ex + ey * ey);
        if (d < bestD) { bestD = d; best = (int)e; bestT = t; }
    }
    if (best < 0) {
        flashStatus(tr("Cuchillo: no hay ninguna arista bajo el cursor."));
        return 0;
    }
    MeshEditResult r = loopCut(*selected->mesh, topo, best, bestT);
    if (!r.changed) {
        flashStatus(tr("Cuchillo: esa arista no tiene un bucle de cuadriláteros que cortar."));
        return 0;
    }
    history.commitBefore(root);
    selected->mesh = std::make_shared<const MeshData>(std::move(r.mesh));
    if (g_vp) g.comp.sel.clear();
    flashStatus(tr("Cuchillo: ") + std::to_string(r.changed) + (r.changed == 1 ? tr(" polígono cortado.") : tr(" polígonos cortados.")));
    return r.changed;
}

// Cuchillo en modo Plano: el clic fija un punto sobre la malla (el primer triangulo que toca el rayo) y el
// plano que corta pasa por el con el eje local elegido como normal.
int knifePlaneAt(SceneNode& root, SceneNode* selected, const OrbitalCamera& camera, const ViewportState& vp,
                 SceneHistory& history, float mx, float my) {
    if (!editableMesh(selected)) {
        flashStatus(tr("Cuchillo: selecciona un objeto editable (C)."));
        return 0;
    }
    const glm::mat4 view = camera.viewMatrix();
    const glm::mat4 proj = camera.projectionMatrix(vp.w / std::max(1.0f, vp.h), true);  // la que espera screenRay
    const glm::vec3 eye = glm::vec3(glm::inverse(view)[3]);
    const Ray ray = TransformGizmo::screenRay(mx - vp.x, my - vp.y, (int)vp.w, (int)vp.h, view, proj, eye);
    const glm::mat4 invW = glm::inverse(selected->worldMatrix());
    const glm::vec3 o = glm::vec3(invW * glm::vec4(ray.origin, 1.0f));
    const glm::vec3 dir = glm::vec3(invW * glm::vec4(ray.direction, 0.0f));
    const MeshData& m = *selected->mesh;
    float bestT = 1e30f;
    for (size_t i = 0; i + 2 < m.indices.size(); i += 3) {
        const glm::vec3 a = m.vertices[m.indices[i]].position, b = m.vertices[m.indices[i + 1]].position,
                        c = m.vertices[m.indices[i + 2]].position;
        const glm::vec3 e1 = b - a, e2 = c - a, pv = glm::cross(dir, e2);
        const float det = glm::dot(e1, pv);
        if (std::fabs(det) < 1e-12f) continue;
        const glm::vec3 tv = o - a;
        const float u = glm::dot(tv, pv) / det;
        if (u < 0.0f || u > 1.0f) continue;
        const glm::vec3 qv = glm::cross(tv, e1);
        const float v = glm::dot(dir, qv) / det;
        if (v < 0.0f || u + v > 1.0f) continue;
        const float t = glm::dot(e2, qv) / det;
        if (t > 1e-6f && t < bestT) bestT = t;
    }
    if (bestT > 1e29f) {
        flashStatus(tr("Cuchillo: el clic no toca la malla."));
        return 0;
    }
    const glm::vec3 hit = o + dir * bestT;
    glm::vec3 nL(0.0f);
    nL[std::clamp(g.knifeAxis, 0, 2)] = 1.0f;
    const MeshTopology topo = buildMeshTopology(m);
    std::vector<char> allowed;
    if (g_vp && g_vp->compMode == 3 && syncComponents(selected) && !g.comp.sel.empty()) {
        allowed.assign(topo.polys.size(), 0);
        for (int p : g.comp.sel) if (p < (int)allowed.size()) allowed[(size_t)p] = 1;
    }
    MeshEditResult r = knifeCut(m, topo, hit, nL, allowed, nullptr);
    if (!r.changed) {
        flashStatus(tr("Cuchillo: el plano no corta ningún polígono."));
        return 0;
    }
    history.commitBefore(root);
    selected->mesh = std::make_shared<const MeshData>(std::move(r.mesh));
    if (g_vp) g.comp.sel.clear();
    flashStatus(tr("Cuchillo: ") + std::to_string(r.changed) + (r.changed == 1 ? tr(" polígono cortado.") : tr(" polígonos cortados.")));
    return r.changed;
}

void modelToolMouse(SceneNode& root, SceneNode* selected, const OrbitalCamera& camera, float mx, float my,
                    bool leftDown, bool leftPressed, SceneHistory& history) {
    auto& d = g.tdrag;
    if (!g_vp) return;
    if (leftPressed) {
        d = {};
        d.startX = d.curX = mx;
        d.startY = d.curY = my;
        if (g.modelTool == UiState::ModelTool::Knife && g.knifePlane) {
            knifePlaneAt(root, selected, camera, *g_vp, history, mx, my);
            d = {};
            return;
        }
        if (g.modelTool == UiState::ModelTool::Knife && g.knifeLoop) {
            knifeLoopAt(root, selected, camera, *g_vp, history, mx, my);
            d = {};
            return;
        }
        if (g.modelTool == UiState::ModelTool::Knife) {
            d.active = editableMesh(selected) != nullptr;
            if (!d.active) flashStatus(tr("Cuchillo: selecciona un objeto editable (C)."));
            return;
        }
        if (!syncComponents(selected) || g.comp.mode != 3 || g.comp.sel.empty()) {
            flashStatus(std::string(modelToolName(g.modelTool)) + tr(": selecciona polígonos de un objeto editable."));
            return;
        }
        d.active = true;
        d.startMesh = selected->mesh;
        d.startTopo = g.comp.topo;
        d.startSel = g.comp.sel;
        d.base = toolValue();
        return;
    }
    if (!d.active) return;
    if (leftDown) {
        d.curX = mx;
        d.curY = my;
        if (g.modelTool == UiState::ModelTool::Knife || !selected || (!d.applied && selected->mesh != d.startMesh))
            return;
        // Pixeles -> cm a la profundidad de la seleccion.
        glm::vec3 c(0.0f);
        for (int p : d.startSel) c += polyCenter(d.startTopo, p);
        c /= (float)std::max<size_t>(1, d.startSel.size());
        const glm::mat4 proj = camera.projectionMatrix(g_vp->w / std::max(1.0f, g_vp->h), false);
        const glm::vec4 clip = proj * camera.viewMatrix() * selected->worldMatrix() * glm::vec4(c, 1.0f);
        const float unitsPerPx = 2.0f * std::max(1e-4f, clip.w) / (std::max(1e-6f, proj[1][1]) * std::max(1.0f, g_vp->h));
        float v = d.base + (mx - d.startX) * unitsPerPx * kCmPerUnit;
        if (g.modelTool != UiState::ModelTool::Extrude) v = std::max(0.0f, v);
        v = std::round(v * 10.0f) / 10.0f;
        if (v == d.lastValue) return;
        if (!d.applied) {
            history.commitBefore(root);  // un paso de deshacer por arrastre
            d.applied = true;
        }
        d.lastValue = v;
        toolValue() = v;
        applyPolyTool(*selected, d.startMesh, d.startTopo, d.startSel, v, g.bevelExtrudeCm);
        return;
    }
    // Soltar.
    if (g.modelTool == UiState::ModelTool::Knife)
        knifeLine(root, selected, camera, *g_vp, history, ImVec2(d.startX, d.startY), ImVec2(d.curX, d.curY));
    d = {};
}

void drawModelToolUi(SceneNode* selected, const ViewportState& vp) {
    using MT = UiState::ModelTool;
    if (g.modelTool == MT::None) return;
    if (g.modelTool == MT::Knife && g.tdrag.active) {
        ImDrawList* dl = ImGui::GetForegroundDrawList();
        dl->AddLine(ImVec2(g.tdrag.startX, g.tdrag.startY), ImVec2(g.tdrag.curX, g.tdrag.curY), IM_COL32(255, 220, 60, 255), 2.0f);
    }
    ImGui::SetNextWindowPos(ImVec2(vp.x + vp.w - 12.0f, vp.y + 36.0f), ImGuiCond_Always, ImVec2(1.0f, 0.0f));
    ImGui::SetNextWindowBgAlpha(0.92f);
    bool open = true;
    const std::string title = std::string(modelToolName(g.modelTool)) + "###modeltool";
    if (ImGui::Begin(title.c_str(), &open,
                     ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::SetNextItemWidth(120.0f);
        if (g.modelTool == MT::Extrude) {
            ImGui::DragFloat(tr("Desplazamiento (cm)"), &g.extrudeCm, 0.5f, -10000.0f, 10000.0f, "%.1f");
        } else if (g.modelTool == MT::InnerExtrude) {
            ImGui::DragFloat(tr("Desplazamiento (cm)"), &g.innerInsetCm, 0.5f, 0.0f, 10000.0f, "%.1f");
        } else if (g.modelTool == MT::Bevel) {
            ImGui::DragFloat(tr("Desplazamiento de Inserción (cm)"), &g.bevelInsetCm, 0.5f, 0.0f, 10000.0f, "%.1f");
            ImGui::SetNextItemWidth(120.0f);
            ImGui::DragFloat(tr("Extrusión (cm)"), &g.bevelExtrudeCm, 0.5f, -10000.0f, 10000.0f, "%.1f");
        } else {
            if (ImGui::RadioButton(tr("Línea"), !g.knifeLoop && !g.knifePlane)) { g.knifeLoop = false; g.knifePlane = false; }
            ImGui::SameLine();
            if (ImGui::RadioButton(tr("Plano"), g.knifePlane)) { g.knifeLoop = false; g.knifePlane = true; }
            ImGui::SameLine();
            if (ImGui::RadioButton(tr("Bucle"), g.knifeLoop)) { g.knifeLoop = true; g.knifePlane = false; }
            if (g.knifePlane) {
                ImGui::SetNextItemWidth(90.0f);
                const char* axes[] = {"X", "Y", "Z"};
                ImGui::Combo(tr("Eje"), &g.knifeAxis, axes, 3);
            }
            ImGui::Checkbox(tr("Solo Visibles"), &g.knifeVisibleOnly);
            if (g.knifePlane)
                ImGui::TextDisabled(tr("Modo Plano: clic en la malla; corta con un plano\nperpendicular al eje elegido que pasa por ese punto."));
            else if (g.knifeLoop)
                ImGui::TextDisabled(tr("Modo Bucle: clic en una arista; corta todo su anillo\npor el punto donde haces clic."));
            else
                ImGui::TextDisabled(tr("Modo Línea: corta lo que cruza la línea.\nCon polígonos seleccionados, solo esos."));
        }
        if (g.modelTool != MT::Knife) {
            if (ImGui::Button(tr("Aplicar"))) g.toolApplyRequested = true;
            ImGui::SameLine();
            ImGui::TextDisabled(editableMesh(selected) ? tr("o arrastra en el visor") : tr("(objeto editable)"));
        }
    }
    ImGui::End();
    if (!open) setModelTool(MT::None);
}

// --ui-cmd: tool=extrude:<cm> | tool=inner:<cm> | tool=bevel:<cm>,<cm> | knife=x0,y0,x1,y1 (fracciones del visor)
void runPendingToolCmd(SceneNode& root, SceneNode*& selected, const OrbitalCamera& camera, const ViewportState& vp,
                       SceneHistory& history) {
    const std::string cmd = g.pendingTool;
    if (cmd.rfind("create=", 0) == 0) {  // create=<nombre de la paleta de Objetos>
        // varios seguidos con "+": create=Cápsula+Doblar crea la capsula y le pone un deformador
        std::stringstream names(cmd.substr(7));
        std::string one;
        while (std::getline(names, one, '+')) {
            for (const PaletteItem& it : kPrimitives)
                if (it.functional && one == it.name.key) createPaletteItem(root, selected, history, it);
            for (const PaletteItem& it : kDeformers)
                if (it.functional && one == it.name.key) createPaletteItem(root, selected, history, it);
            for (const PaletteItem& it : kMoGraph)
                if (it.functional && one == it.name.key) createPaletteItem(root, selected, history, it);
            for (const PaletteItem* list : {kGenerators, kModeling, kScene, kCameras, kLights}) {
                const int cnt = list == kGenerators ? countOf(kGenerators) : list == kModeling ? countOf(kModeling)
                              : list == kScene ? countOf(kScene) : list == kCameras ? countOf(kCameras) : countOf(kLights);
                for (int i = 0; i < cnt; ++i)
                    if (list[i].functional && one == list[i].name.key) createPaletteItem(root, selected, history, list[i]);
            }
        }
        return;
    }
    std::vector<float> nums;
    {
        const size_t at = cmd.find_first_of(":=", cmd.rfind("tool=", 0) == 0 ? 5 : 0);
        std::stringstream ss(at == std::string::npos ? "" : cmd.substr(at + 1));
        std::string part;
        try {
            while (std::getline(ss, part, ',')) nums.push_back(std::stof(part));
        } catch (...) { return; }
    }
    using MT = UiState::ModelTool;
    if (cmd.rfind("plane=", 0) == 0) {
        if (nums.size() != 3) return;
        setModelTool(MT::Knife);
        g.knifeLoop = false;
        g.knifePlane = true;
        g.knifeAxis = (int)nums[2];
        knifePlaneAt(root, selected, camera, vp, history, vp.x + nums[0] * vp.w, vp.y + nums[1] * vp.h);
        return;
    }
    if (cmd.rfind("loop=", 0) == 0) {
        if (nums.size() != 2) return;
        setModelTool(MT::Knife);
        g.knifeLoop = true;
        knifeLoopAt(root, selected, camera, vp, history, vp.x + nums[0] * vp.w, vp.y + nums[1] * vp.h);
        return;
    }
    if (cmd.rfind("knife=", 0) == 0) {
        if (nums.size() != 4) return;
        setModelTool(MT::Knife);
        knifeLine(root, selected, camera, vp, history, ImVec2(vp.x + nums[0] * vp.w, vp.y + nums[1] * vp.h),
                  ImVec2(vp.x + nums[2] * vp.w, vp.y + nums[3] * vp.h));
        return;
    }
    if (nums.empty()) return;
    if (cmd.rfind("tool=extrude", 0) == 0) { setModelTool(MT::Extrude); g.extrudeCm = nums[0]; }
    else if (cmd.rfind("tool=inner", 0) == 0) { setModelTool(MT::InnerExtrude); g.innerInsetCm = nums[0]; }
    else if (cmd.rfind("tool=bevel", 0) == 0) {
        setModelTool(MT::Bevel);
        g.bevelInsetCm = nums[0];
        if (nums.size() > 1) g.bevelExtrudeCm = nums[1];
    } else return;
    applyToolNow(root, selected, history);
}

void releaseUiTextures() {
    freeUiTexture(g.pv.tex);
    g.pv.tex = nullptr;
}

bool confirmQuit(SceneNode& root, SceneHistory& history, std::vector<Material>& materials) {
    return confirmQuitImpl(root, history, materials);
}

void drawMainUi(SceneNode& root, SceneNode*& selected, GizmoMode& gizmoMode,
                 const std::filesystem::path& scenesDir, SceneHistory& history, float framerate,
                 RendererBackend activeBackend, Preferences& prefs, const std::filesystem::path& prefsPath,
                 std::vector<Material>& materials, OrbitalCamera& camera, ViewportState& viewport) {
    g_vp = &viewport;
    camera.nav.pan = (float)prefs.navPan / 100.0f;
    camera.nav.zoom = (float)prefs.navZoom / 100.0f;
    camera.nav.orbit = (float)prefs.navOrbit / 100.0f;
    camera.nav.invertOrbit = prefs.navInvertOrbit;
    if (!g.cleanInit) {  // primer fotograma / tras Nuevo o Abrir: la escena actual es el punto guardado
        g.cleanSnap = history.snapshot(root);
        g.cleanInit = true;
    }
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
    // IK en vivo: las cadenas con etiqueta IK activa siguen a su objetivo cada fotograma (mover el
    // controlador mueve la cadena, como en C4D). Una cadena ya resuelta converge en una iteracion.
    solveAllIk(root);
    clampProtectionLimits(root); // etiquetas Proteccion con "Permitir expresiones" apagado: limitan tambien a la IK
    // Piel en vivo: las mallas con pesos siguen a las articulaciones (no hace nada si no se han movido).
    evaluateSkins(root);
    // Objetos Simetria: reflejan a su hijo (despues de la Piel y la animacion, que son las que lo mueven).
    evaluateSymmetry(root);
    evaluateDeformers(root);
    evaluateCloners(root);
    evaluateInstances(root);
    if (g.recordRequested) {
        g.recordRequested = false;
        recordActiveObjects(root, selected, &history);
    }
    g.selKeys = selected ? keyFrames(*selected) : std::vector<int>{};
    for (auto it = g.keySel.begin(); it != g.keySel.end();)  // solo claves que existen
        it = std::find(g.selKeys.begin(), g.selKeys.end(), *it) == g.selKeys.end() ? g.keySel.erase(it) : std::next(it);
    applyKeyRequest(root, selected, history);

    handleShortcuts(root, selected, gizmoMode, history, materials, camera, viewport);
    runPendingCmd(root, selected, history, camera);
    // --ui-cmd=comp=...: despues de los demas comandos (p. ej. make-editable).
    if (g.pendingCompMode && g.cmdQueue.empty() && g.pendingCmd == UiCmd::None) {
        viewport.compMode = std::clamp(g.pendingCompMode, 0, 3);
        g.pendingCompMode = 0;
        if (syncComponents(selected)) g.comp.sel.insert(g.pendingCompSel.begin(), g.pendingCompSel.end());
    }
    if (g.compMoveRequested && !g.pendingCompMode && !g.compSelectAllRequested && g.compOp.empty() && g.pendingTool.empty() &&
        g.cmdQueue.empty() && viewport.w > 64.0f && ImGui::GetFrameCount() > 8) {
        g.compMoveRequested = false;
        SceneNode* tgt = componentGizmoTarget(selected, false);
        if (tgt && selected && selected->mesh) {
            history.commitBefore(root);
            beginComponentDrag(*selected);
            tgt->position += g.compMoveCm / kCmPerUnit;
            applyComponentDrag(*selected);
            // Resumen para la verificacion: cuantos puntos se movieron y la simetria resultante.
            const auto& vs = selected->mesh->vertices;
            const int ax = std::max(0, g.symAxis);
            int asym = 0;
            for (const auto& a : vs) {
                glm::vec3 mp = a.position;
                mp[ax] = -mp[ax];
                float best = 1e9f;
                for (const auto& b : vs) best = std::min(best, glm::length(b.position - mp));
                if (best > 1e-3f) ++asym;
            }
            int up = 0;
            for (const auto& a : vs) if (a.position.y > 1e-4f) ++up;
            if (const char* tmp = std::getenv("TEMP")) {
                const std::string fn = std::string(tmp) + "/iron3d_symtest.txt";
                if (FILE* f = std::fopen(fn.c_str(), "a")) {
                    std::fprintf(f, "sym=%d vertices=%zu elevados=%d sin_espejo=%d\n", g.symAxis, vs.size(), up, asym);
                    std::fclose(f);
                }
            }
            flashStatus(tr("comp-move: ") + std::to_string(asym) + tr(" vertices sin espejo."));
        }
    }
    if (g.symObjRequested && !g.compMoveRequested && !g.pendingCompMode && g.cmdQueue.empty() && ImGui::GetFrameCount() > 12) {
        g.symObjRequested = false;
        viewport.compMode = 0;
        const PaletteItem it{tr("Simetría"), Icon::ModSymmetry, PrimitiveType::None, true};
        createPaletteItem(root, selected, history, it);
    }
    if (g.compSelectAllRequested && !g.pendingCompMode && viewport.w > 64.0f && ImGui::GetFrameCount() > 5) {
        const bool keepVis = g.selVisibleOnly;
        g.selVisibleOnly = g.compSelectAllRequested == 1;
        componentMarquee(selected, camera, viewport, [](ImVec2) { return true; }, false, false);
        g.selVisibleOnly = keepVis;
        g.compSelectAllRequested = 0;
    }
    if (g.toolApplyRequested) {
        g.toolApplyRequested = false;
        applyToolNow(root, selected, history);
    }
    if (!g.pendingTool.empty() && !g.pendingCompMode && !g.compSelectAllRequested && viewport.w > 64.0f &&
        ImGui::GetFrameCount() > 5) {
        runPendingToolCmd(root, selected, camera, viewport, history);
        g.pendingTool.clear();
    }
    if (g.compDeleteRequested && !g.pendingCompMode) {
        g.compDeleteRequested = false;
        deleteComponents(root, selected, history);
    }
    if (!g.compOp.empty() && !g.pendingCompMode && !g.compSelectAllRequested) {
        const std::string op = g.compOp;
        g.compOp.clear();
        runCompOp(root, selected, history, op);
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
    drawModelToolUi(selected, viewport);
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
    drawObjectInfoDialog(selected);
    drawImportReport();
    drawExportReport();
    drawSavePresetDialog();
    drawCommander(root, selected, gizmoMode, history, materials, camera, viewport);
    drawCurveEditor(selected);
    if (g.renderIn > 0 && --g.renderIn == 0) g.renderRequested = true;  // --ui-cmd=render: espera a que los create= anteriores existan
    // --ui-cmd=render-save=W,H,yaw,pitch,dist,tx,ty,tz,ruta.png: renderiza con esa camara, guarda el PNG y sale.
    if (g.renderSaveIn > 0 && --g.renderSaveIn == 0) {
        int w = 1000, h = 1000;
        float yaw = 0, pitch = 0, dist = 4, tx = 0, ty = 0, tz = 0;
        int used = 0;
        if (std::sscanf(g.renderSave.c_str(), "%d,%d,%f,%f,%f,%f,%f,%f,%n", &w, &h, &yaw, &pitch, &dist, &tx, &ty, &tz, &used) >= 8 && used > 0) {
            OrbitalCamera::ViewState vs;
            vs.view = OrbitalCamera::View::Perspective;
            vs.target = glm::vec3(tx, ty, tz);
            vs.distance = dist;
            vs.yaw = glm::radians(yaw);
            vs.pitch = glm::radians(pitch);
            camera.restoreView(vs);
            g.render.width = w;
            g.render.height = h;
            g.render.useViewportSize = false;
            g.render.antialias = 3;
            renderToPictureViewer(root, materials, camera, viewport);
            const bool ok = savePng(std::filesystem::path(g.renderSave.substr((size_t)used)), g.pv.image);
            std::fprintf(stderr, "[Iron3D] render-save %s\n", ok ? "ok" : "ERROR");
            std::fflush(stderr);
            std::exit(ok ? 0 : 3);
        }
    }
    if (g.renderRequested) {
        g.renderRequested = false;
        renderToPictureViewer(root, materials, camera, viewport);
    }
    drawRenderSettings();
    drawTextureMode(root, selected, history, viewport, materials);
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
    ImGui::Text(tr("Iron3D Studio - hoja de iconos (%d)"), total);
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
    else if (cmd == "sym-obj") g.symObjRequested = true;
    else if (cmd == "cut") g.cmdQueue.push_back(UiCmd::Cut);
    else if (cmd == "copy") g.cmdQueue.push_back(UiCmd::Copy);
    else if (cmd == "paste") g.cmdQueue.push_back(UiCmd::Paste);
    else if (cmd == "current-state") g.cmdQueue.push_back(UiCmd::CurrentState);
    else if (cmd == "connect") g.cmdQueue.push_back(UiCmd::Connect);
    else if (cmd == "bool-union") g.cmdQueue.push_back(UiCmd::BoolUnion);
    else if (cmd == "bool-sub") g.cmdQueue.push_back(UiCmd::BoolSubtract);
    else if (cmd == "bool-int") g.cmdQueue.push_back(UiCmd::BoolIntersect);
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
    else if (cmd == "render-fx") { g.render.studio = true; g.render.shadow = true; }
    else if (cmd.rfind("render-save=", 0) == 0) { g.renderSave = cmd.substr(12); g.renderSaveIn = 25; }
    else if (cmd == "key") g.recordRequested = true;
    else if (cmd == "curves") g.showCurves = true;
    else if (cmd == "keys-del") g.keyReq = UiState::KeyReq::Delete;
    else if (cmd.rfind("keys-sel=", 0) == 0 || cmd.rfind("keys-move=", 0) == 0 || cmd.rfind("keys-interp=", 0) == 0) {
        const size_t eq = cmd.find('=');
        const std::string v = cmd.substr(eq + 1);
        try {
            if (cmd[5] == 's') {
                std::stringstream ss(v);
                std::string part;
                g.keySel.clear();
                while (std::getline(ss, part, ',')) g.keySel.insert(std::stoi(part));
            } else if (cmd[5] == 'm') {
                g.keyReq = UiState::KeyReq::Move;
                g.keyReqDelta = std::stoi(v);
            } else {
                g.keyReq = UiState::KeyReq::Interp;
                g.keyReqInterp = v == "l" ? KeyInterp::Linear : v == "s" ? KeyInterp::Step : KeyInterp::Spline;
            }
        } catch (...) { return false; }
    }
    else if (cmd == "comp-del") g.compDeleteRequested = true;
    else if (cmd == "obj-info") g.showObjInfo = true;
    else if (cmd == "about") g.showAbout = true;
    else if (cmd.rfind("prefs-page=", 0) == 0) {
        try { g.prefsPageReq = std::stoi(cmd.substr(11)); } catch (...) { return false; }
        g.showPrefs = true;
    }
    else if (cmd.rfind("menu=", 0) == 0) {  // menu=Malla,Comandos: nombres tal como se ven
        std::stringstream ss(cmd.substr(5));
        std::string part;
        while (std::getline(ss, part, ',')) g.dbgMenus.insert(part);
    }
    else if (cmd.rfind("sym=", 0) == 0) {  // sym=off|x|y|z
        const std::string v = cmd.substr(4);
        g.symAxis = v == "x" ? 0 : v == "y" ? 1 : v == "z" ? 2 : -1;
    }
    else if (cmd.rfind("comp-move=", 0) == 0) {
        float x = 0, y = 0, z = 0;
        if (std::sscanf(cmd.c_str() + 10, "%f,%f,%f", &x, &y, &z) != 3) return false;
        g.compMoveCm = glm::vec3(x, y, z);
        g.compMoveRequested = true;
    }
    else if (cmd.rfind("meshop=", 0) == 0) g.compOp = cmd.substr(7);
    else if (cmd.rfind("tool=", 0) == 0 || cmd.rfind("knife=", 0) == 0 || cmd.rfind("loop=", 0) == 0 || cmd.rfind("plane=", 0) == 0 || cmd.rfind("create=", 0) == 0) g.pendingTool = cmd;
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
    else if (cmd == "render") g.renderIn = 20;  // verificacion: Renderizar Vista Activa sin teclado
    else if (cmd == "close-report") g.showImport = false; // verificacion: el informe de importacion tapa el visor
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
