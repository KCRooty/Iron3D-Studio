#include "MainUi.h"
#include "core/SceneSerializer.h"

#include <imgui.h>

#include <vector>
#include <cstring>

namespace iron3d {

namespace {

// Nombre por defecto al crear cada tipo de primitiva desde el menu contextual
// del Gestor de Objetos -- igual de informativo que el nombre de clase de C4D
// al arrastrar un primitivo nuevo (p.ej. "Cube", "Sphere").
const char* defaultNameFor(PrimitiveType type) {
    switch (type) {
        case PrimitiveType::Cube: return "Cubo";
        case PrimitiveType::Sphere: return "Esfera";
        case PrimitiveType::Plane: return "Plano";
        case PrimitiveType::Light: return "Luz";
        case PrimitiveType::None: default: return "Nulo";
    }
}

void drawNodeTree(SceneNode& node, SceneNode*& selected, SceneNode*& toDelete,
                   SceneNode& sceneRoot, SceneHistory& history) {
    ImGui::PushID(node.id());

    bool isLeaf = node.children().empty();
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick |
                                ImGuiTreeNodeFlags_SpanAvailWidth;
    if (&node == selected) flags |= ImGuiTreeNodeFlags_Selected;
    if (isLeaf) flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;

    ImGui::Checkbox("##vis", &node.visible);
    ImGui::SameLine();
    bool opened = ImGui::TreeNodeEx(node.name().c_str(), flags);
    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
        selected = &node;
    }

    SceneNode* pendingAddTo = nullptr;
    PrimitiveType pendingAddType = PrimitiveType::None;
    SceneNode* pendingDuplicate = nullptr;
    if (ImGui::BeginPopupContextItem("ctx")) {
        if (ImGui::BeginMenu("Anadir")) {
            if (ImGui::MenuItem("Nulo (vacio)")) { pendingAddTo = &node; pendingAddType = PrimitiveType::None; }
            if (ImGui::MenuItem("Cubo")) { pendingAddTo = &node; pendingAddType = PrimitiveType::Cube; }
            if (ImGui::MenuItem("Esfera")) { pendingAddTo = &node; pendingAddType = PrimitiveType::Sphere; }
            if (ImGui::MenuItem("Plano")) { pendingAddTo = &node; pendingAddType = PrimitiveType::Plane; }
            if (ImGui::MenuItem("Luz")) { pendingAddTo = &node; pendingAddType = PrimitiveType::Light; }
            ImGui::EndMenu();
        }
        bool canDelete = node.parent() != nullptr; // la raiz de la escena no se borra ni se duplica
        if (ImGui::MenuItem("Duplicar", "Ctrl+D", false, canDelete)) pendingDuplicate = &node;
        if (ImGui::MenuItem("Eliminar", nullptr, false, canDelete)) toDelete = &node;
        ImGui::EndPopup();
    }

    if (opened && !isLeaf) {
        // Copia de los punteros hijos ANTES de recursar: anadir/eliminar se aplica
        // despues de este bucle, nunca durante, para no invalidar el vector en marcha.
        std::vector<SceneNode*> childPtrs;
        childPtrs.reserve(node.children().size());
        for (const auto& c : node.children()) childPtrs.push_back(c.get());
        for (SceneNode* c : childPtrs) drawNodeTree(*c, selected, toDelete, sceneRoot, history);
        ImGui::TreePop();
    }

    if (pendingAddTo) {
        history.commitBefore(sceneRoot);
        pendingAddTo->addChild(defaultNameFor(pendingAddType), pendingAddType);
    }
    if (pendingDuplicate && pendingDuplicate->parent()) {
        history.commitBefore(sceneRoot);
        selected = duplicateSubtree(*pendingDuplicate->parent(), *pendingDuplicate);
    }
    ImGui::PopID();
}

void drawAttributesPanel(SceneNode* selected, SceneNode& sceneRoot, SceneHistory& history) {
    ImGui::Begin("Atributos");
    if (!selected) {
        ImGui::TextDisabled("Ningun objeto seleccionado.");
    } else {
        // Patron activar/confirmar de ImGui para deshacer: un campo se puede
        // editar durante varios frames seguidos (arrastrar, escribir), asi
        // que el "antes" se toma solo al primer frame en que el campo se
        // activa -- `preWidgetSnap` es el estado justo antes de que CUALQUIER
        // campo de este panel toque nada este frame, capturado antes de
        // dibujar el primero -- y se empuja a la pila una sola vez, al
        // soltar el campo. Nunca uno por frame/tecla, o la pila se llenaria
        // de ruido.
        const std::string preWidgetSnap = history.snapshot(sceneRoot);
        static std::string pendingSnap;
        auto trackEdit = [&]() {
            if (ImGui::IsItemActivated()) pendingSnap = preWidgetSnap;
            if (ImGui::IsItemDeactivatedAfterEdit()) history.commit(pendingSnap);
        };

        char nameBuf[128];
        std::snprintf(nameBuf, sizeof(nameBuf), "%s", selected->name().c_str());
        if (ImGui::InputText("Nombre", nameBuf, sizeof(nameBuf))) {
            selected->setName(nameBuf);
        }
        trackEdit();
        ImGui::TextDisabled("Tipo: %s", primitiveTypeName(selected->primitive));
        ImGui::Separator();
        ImGui::DragFloat3("Posicion", &selected->position.x, 0.05f);
        trackEdit();
        ImGui::DragFloat3("Rotacion", &selected->rotationDeg.x, 1.0f);
        trackEdit();
        ImGui::DragFloat3("Escala", &selected->scale.x, 0.05f, 0.01f, 100.0f);
        trackEdit();
        ImGui::Separator();
        ImGui::Checkbox("Visible", &selected->visible);
        trackEdit();
        ImGui::TextDisabled("%zu hijo(s)", selected->children().size());
    }
    ImGui::End();
}

// Guardar/cargar escena a disco (Fase 2c, formato .iron -- ver SceneSerializer.h).
// `root` se pasa por puntero crudo al estado persistente del panel en vez de
// alla global para no acoplar esto a mas parametros de drawMainUi de los
// necesarios; vive solo mientras el programa corre, no hay multi-ventana.
void drawScenePanel(SceneNode& root, SceneNode*& selected, const std::filesystem::path& scenesDir,
                     SceneHistory& history) {
    static char filenameBuf[128] = "escena";
    static std::string statusMsg;
    static bool statusIsError = false;

    ImGui::Begin("Escena");
    ImGui::InputText("Archivo", filenameBuf, sizeof(filenameBuf));
    ImGui::TextDisabled(".iron en %s", scenesDir.string().c_str());

    const std::filesystem::path path = scenesDir / (std::string(filenameBuf) + ".iron");

    if (ImGui::Button("Guardar")) {
        std::string err;
        if (saveScene(path, root, &err)) {
            statusMsg = "Guardado: " + path.filename().string();
            statusIsError = false;
        } else {
            statusMsg = "Error al guardar: " + err;
            statusIsError = true;
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Cargar")) {
        std::string err;
        if (loadScene(path, root, &err)) {
            selected = nullptr; // la escena entera cambio, cualquier puntero viejo es invalido
            history.reset(); // un deshacer no debe saltar a una escena que ya no existe
            statusMsg = "Cargado: " + path.filename().string();
            statusIsError = false;
        } else {
            statusMsg = "Error al cargar: " + err;
            statusIsError = true;
        }
    }
    if (!statusMsg.empty()) {
        ImGui::TextColored(statusIsError ? ImVec4(0.95f, 0.35f, 0.35f, 1.0f) : ImVec4(0.4f, 0.9f, 0.4f, 1.0f),
                            "%s", statusMsg.c_str());
    }
    ImGui::End();
}

// Barra de herramientas: modo del gizmo (mover/rotar/escalar, atajos 1/2/3 --
// numeros simples en vez de W/E/R de otros DCC porque el proyecto no tiene
// ninguna convencion de teclado propia todavia fijada por el Documento
// Maestro) + deshacer/rehacer/duplicar (Fase 2d, Ctrl+Z/Ctrl+Y o Ctrl+Shift+Z/
// Ctrl+D). Un undo/redo reconstruye el arbol entero (ver SceneHistory), asi
// que cualquier seleccion vieja queda invalida y se limpia aqui mismo.
void drawToolsToolbar(GizmoMode& mode, SceneNode& root, SceneNode*& selected, SceneHistory& history) {
    ImGuiIO& io = ImGui::GetIO();
    if (!io.WantCaptureKeyboard) {
        if (ImGui::IsKeyPressed(ImGuiKey_1)) mode = GizmoMode::Translate;
        if (ImGui::IsKeyPressed(ImGuiKey_2)) mode = GizmoMode::Rotate;
        if (ImGui::IsKeyPressed(ImGuiKey_3)) mode = GizmoMode::Scale;

        if (io.KeyCtrl && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Z)) {
            if (history.undo(root)) selected = nullptr;
        }
        if (io.KeyCtrl && ((io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Z)) || ImGui::IsKeyPressed(ImGuiKey_Y))) {
            if (history.redo(root)) selected = nullptr;
        }
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_D) && selected && selected->parent()) {
            history.commitBefore(root);
            selected = duplicateSubtree(*selected->parent(), *selected);
        }
    }

    ImGui::Begin("Herramientas");
    auto modeButton = [&](const char* label, GizmoMode m) {
        bool active = (mode == m);
        if (active) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        if (ImGui::Button(label)) mode = m;
        if (active) ImGui::PopStyleColor();
    };
    modeButton("Mover (1)", GizmoMode::Translate);
    ImGui::SameLine();
    modeButton("Rotar (2)", GizmoMode::Rotate);
    ImGui::SameLine();
    modeButton("Escalar (3)", GizmoMode::Scale);

    ImGui::Separator();
    if (!history.canUndo()) ImGui::BeginDisabled();
    if (ImGui::Button("Deshacer (Ctrl+Z)")) {
        if (history.undo(root)) selected = nullptr;
    }
    if (!history.canUndo()) ImGui::EndDisabled();
    ImGui::SameLine();
    if (!history.canRedo()) ImGui::BeginDisabled();
    if (ImGui::Button("Rehacer (Ctrl+Y)")) {
        if (history.redo(root)) selected = nullptr;
    }
    if (!history.canRedo()) ImGui::EndDisabled();
    ImGui::End();
}

// Eleccion de backend de render (Fase 3c): solo guarda en disco, nunca
// cambia el renderer en caliente -- `activeBackend` (el que ya esta
// corriendo) se muestra aparte para que quede claro por que elegir otro no
// hace nada hasta reiniciar, igual que Cinema 4D con su API de render.
void drawPreferencesPanel(RendererBackend activeBackend, Preferences& prefs,
                           const std::filesystem::path& prefsPath) {
    static std::string statusMsg;

    ImGui::Begin("Preferencias");
    ImGui::Text("Backend activo esta sesion: %s", activeBackend == RendererBackend::Vulkan ? "Vulkan" : "OpenGL");
    ImGui::Separator();
    ImGui::TextUnformatted("Backend de render (se aplica al reiniciar):");
    int backendIdx = (prefs.renderBackend == RendererBackend::Vulkan) ? 0 : 1;
    if (ImGui::RadioButton("Vulkan", backendIdx == 0)) prefs.renderBackend = RendererBackend::Vulkan;
    ImGui::SameLine();
    if (ImGui::RadioButton("OpenGL", backendIdx == 1)) prefs.renderBackend = RendererBackend::OpenGL;

    if (ImGui::Button("Guardar")) {
        statusMsg = savePreferences(prefsPath, prefs) ? "Guardado. Reinicia Iron3D Studio para aplicarlo."
                                                        : "Error al guardar las preferencias.";
    }
    if (!statusMsg.empty()) ImGui::TextUnformatted(statusMsg.c_str());
    ImGui::End();
}

} // namespace

void drawMainUi(SceneNode& root, SceneNode*& selected, GizmoMode& gizmoMode,
                 const std::filesystem::path& scenesDir, SceneHistory& history, float framerate,
                 RendererBackend activeBackend, Preferences& prefs, const std::filesystem::path& prefsPath) {
    SceneNode* toDelete = nullptr;

    drawToolsToolbar(gizmoMode, root, selected, history);
    drawScenePanel(root, selected, scenesDir, history);
    drawPreferencesPanel(activeBackend, prefs, prefsPath);

    ImGui::Begin("Gestor de Objetos");
    drawNodeTree(root, selected, toDelete, root, history);
    ImGui::Separator();
    ImGui::TextUnformatted("Navegacion (estilo C4D):");
    ImGui::BulletText("Alt + clic izq: orbitar");
    ImGui::BulletText("Alt + clic medio: panear");
    ImGui::BulletText("Alt + clic der / rueda: zoom");
    ImGui::BulletText("Clic en un eje del gizmo: mover/rotar/escalar");
    ImGui::BulletText("Clic derecho en un nodo: anadir primitiva / duplicar / eliminar");
    ImGui::BulletText("Ctrl+Z / Ctrl+Y: deshacer / rehacer -- Ctrl+D: duplicar seleccionado");
    ImGui::Separator();
    ImGui::Text("%.1f FPS (%.2f ms/frame)", framerate, 1000.0f / framerate);
    ImGui::End();

    drawAttributesPanel(selected, root, history);

    if (toDelete && toDelete->parent()) {
        history.commitBefore(root);
        toDelete->parent()->removeChild(toDelete); // tras esta linea `toDelete` ya no existe
        selected = nullptr; // evita un puntero colgante si se borro el seleccionado o un ancestro suyo
    }
}

} // namespace iron3d
