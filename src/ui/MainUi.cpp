#include "MainUi.h"

#include <imgui.h>

namespace iron3d {

void drawMainUi(float framerate) {
    ImGui::Begin("Gestor de Objetos (placeholder)");
    ImGui::TextDisabled("Fase 1 -- sin grafo de escena todavia.");
    ImGui::BulletText("Cubo");
    ImGui::Separator();
    ImGui::TextUnformatted("Navegacion (estilo C4D):");
    ImGui::BulletText("Alt + clic izq: orbitar");
    ImGui::BulletText("Alt + clic medio: panear");
    ImGui::BulletText("Alt + clic der / rueda: zoom");
    ImGui::Separator();
    ImGui::Text("%.1f FPS (%.2f ms/frame)", framerate, 1000.0f / framerate);
    ImGui::End();
}

} // namespace iron3d
