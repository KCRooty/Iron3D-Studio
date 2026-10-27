#include "MainUi.h"

#include <imgui.h>

#include <vector>

namespace iron3d {

namespace {

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
    if (ImGui::BeginPopupContextItem("ctx")) {
        if (ImGui::MenuItem("Anadir hijo")) pendingAddTo = &node;
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

    if (pendingAddTo) pendingAddTo->addChild("Nuevo objeto");
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

} // namespace

void drawMainUi(SceneNode& root, SceneNode*& selected, float framerate) {
    SceneNode* toDelete = nullptr;

    ImGui::Begin("Gestor de Objetos");
    drawNodeTree(root, selected, toDelete);
    ImGui::Separator();
    ImGui::TextUnformatted("Navegacion (estilo C4D):");
    ImGui::BulletText("Alt + clic izq: orbitar");
    ImGui::BulletText("Alt + clic medio: panear");
    ImGui::BulletText("Alt + clic der / rueda: zoom");
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
