#pragma once

// Vertice comun a toda la geometria unlit de Iron3D (mallas y gizmo): solo
// posicion + color, sin normales/UV todavia (llegan con materiales PBR en
// una fase posterior). Vivia dentro de CubeMesh.h cuando solo existia el
// cubo; ahora es su propio header porque PrimitiveMesh, MeshData y el gizmo
// de transformacion lo comparten.

#include <glm/glm.hpp>

namespace iron3d {

struct Vertex {
    glm::vec3 position;
    glm::vec3 color;
};

} // namespace iron3d
