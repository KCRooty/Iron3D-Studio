#pragma once

// Vertice comun a toda la geometria de Iron3D (mallas y gizmo): posicion +
// color + normal (Fase 5: la normal llega con la iluminacion/materiales
// basicos). El color se queda para el gizmo (sigue sin iluminar, lo pinta
// BasicPipeline tal cual) y para cualquier malla que no pase por el pipeline
// con iluminacion (LitPipeline la ignora, solo lee posicion+normal).

#include <glm/glm.hpp>

namespace iron3d {

struct Vertex {
    glm::vec3 position;
    glm::vec3 color;
    glm::vec3 normal{0.0f, 0.0f, 1.0f};
};

} // namespace iron3d
