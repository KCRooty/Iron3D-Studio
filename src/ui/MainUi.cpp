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

void drawNodeTree(SceneNode& node, SceneNode*& selected, SceneNode*& toDelete) {
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
    if (ImGui::BeginPopupContextItem("ctx")) {
        if (ImGui::BeginMenu("Anadir")) {
            if (ImGui::MenuItem("Nulo (vacio)")) { pendingAddTo = &node; pendingAddType = PrimitiveType::None; }
            if (ImGui::MenuItem("Cubo")) { pendingAddTo = &node; pendingAddType = PrimitiveType::Cube; }
            if (ImGui::MenuItem("Esfera")) { pendingAddTo = &node; pendingAddType = PrimitiveType::Sphere; }
            if (ImGui::MenuItem("Plano")) { pendingAddTo = &node; pendingAddType = PrimitiveType::Plane; }
            if (ImGui::MenuItem("Luz")) { pendingAddTo = &node; pendingAddType = PrimitiveType::Light; }
            ImGui::EndMenu();
        }
        bool canDelete = node.parent() != nullptr; // la raiz de la escena no se borra
        if (ImGui::MenuItem("Eliminar", nullptr, false, canDelete)) toDelete = &node;
        ImGui::EndPopup();
    }

    if (opened && !isLeaf) {
        // Copia de los punteros hijos ANTES de recursar: anadir/eliminar se aplica
        // despues de este bucle, nunca durante, para no invalidar el vector en marcha.
        std::vector<SceneNode*> childPtrs;
        childPtrs.reserve(node.children().size());
        for (const auto& c : node.children()) childPtrs.push_back(c.get());
        for (SceneNode* c : childPtrs) drawNodeTree(*c, selected, toDelete);
        ImGui::TreePop();
    }

    if (pendingAddTo) pendingAddTo->addChild(defaultNameFor(pendingAddType), pendingAddType);
    ImGui::PopID();
}

void drawAttributesPanel(SceneNode* selected) {
    ImGui::Begin("Atributos");
    if (!selected) {
        ImGui::TextDisabled("Ningun objeto seleccionado.");
    } else {
        char nameBuf[128];
        std::snprintf(nameBuf, sizeof(nameBuf), "%s", selected->name().c_str());
        if (ImGui::InputText("Nombre", nameBuf, sizeof(nameBuf))) {
            selected->setName(nameBuf);
        }
        ImGui::TextDisabled("Tipo: %s", primitiveTypeName(selected->primitive));
        ImGui::Separator();
        ImGui::DragFloat3("Posicion", &selected->position.x, 0.05f);
        ImGui::DragFloat3("Rotacion", &selected->rotationDeg.x, 1.0f);
        ImGui::DragFloat3("Escala", &selected->scale.x, 0.05f, 0.01f, 100.0f);
        ImGui::Separator();
        ImGui::Checkbox("Visible", &selected->visible);
        ImGui::TextDisabled("%zu hijo(s)", selected->children().size());
    }
    ImGui::End();
}

// Guardar/cargar escena a disco (Fase 2c, formato .iron -- ver SceneSerializer.h).
// `root` se pasa por puntero crudo al estado persistente del panel en vez de
// alla global para no acoplar esto a mas parametros de drawMainUi de los
// necesarios; vive solo mientras el programa corre, no hay multi-ventana.
void drawScenePanel(SceneNode& root, SceneNode*& selected, const std::filesystem::path& scenesDir) {
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

// Barra de herramientas del gizmo (mover/rotar/escalar), con atajos 1/2/3 --
// numeros simples en vez de W/E/R de otros DCC porque el proyecto no tiene
// ninguna convencion de teclado propia todavia fijada por el Documento Maestro.
void drawGizmoToolbar(GizmoMode& mode) {
    ImGuiIO& io = ImGui::GetIO();
    if (!io.WantCaptureKeyboard) {
        if (ImGui::IsKeyPressed(ImGuiKey_1)) mode = GizmoMode::Translate;
        if (ImGui::IsKeyPressed(ImGuiKey_2)) mode = GizmoMode::Rotate;
        if (ImGui::IsKeyPressed(ImGuiKey_3)) mode = GizmoMode::Scale;
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
    ImGui::End();
}

} // namespace

void drawMainUi(SceneNode& root, SceneNode*& selected, GizmoMode& gizmoMode,
                 const std::filesystem::path& scenesDir, float framerate) {
    SceneNode* toDelete = nullptr;

    drawGizmoToolbar(gizmoMode);
    drawScenePanel(root, selected, scenesDir);

    ImGui::Begin("Gestor de Objetos");
    drawNodeTree(root, selected, toDelete);
    ImGui::Separator();
    ImGui::TextUnformatted("Navegacion (estilo C4D):");
    ImGui::BulletText("Alt + clic izq: orbitar");
    ImGui::BulletText("Alt + clic medio: panear");
    ImGui::BulletText("Alt + clic der / rueda: zoom");
    ImGui::BulletText("Clic en un eje del gizmo: mover/rotar/escalar");
    ImGui::BulletText("Clic derecho en un nodo: anadir primitiva / eliminar");
    ImGui::Separator();
    ImGui::Text("%.1f FPS (%.2f ms/frame)", framerate, 1000.0f / framerate);
    ImGui::End();

    drawAttributesPanel(selected);

    if (toDelete && toDelete->parent()) {
        toDelete->parent()->removeChild(toDelete); // tras esta linea `toDelete` ya no existe
        selected = nullptr; // evita un puntero colgante si se borro el seleccionado o un ancestro suyo
    }
}

} // namespace iron3d
