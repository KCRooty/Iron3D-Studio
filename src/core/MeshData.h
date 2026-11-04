#pragma once

// Datos de malla en CPU (Fase 2c: mas primitivas que el cubo de la Fase 1)
// para subir a PrimitiveMesh. Con normales desde la Fase 5 (iluminacion
// basica); sin UV/texturas todavia.
//
// Indices de 32 bits (2026-11-04): las mallas importadas de .c4d pasan
// facilmente de los 65 535 vertices que permitia uint16.

#include "Vertex.h"

#include <cstdint>
#include <vector>

namespace iron3d {

struct MeshData {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    // Caja envolvente en espacio local (la rellena updateBounds); la usan la
    // seleccion con clic, Encuadrar y el contorno de seleccion.
    glm::vec3 boundsMin{0.0f};
    glm::vec3 boundsMax{0.0f};
};

void updateBounds(MeshData& mesh);

MeshData makeCubeMesh();
MeshData makeSphereMesh(int rings = 12, int segments = 18);
MeshData makePlaneMesh();

} // namespace iron3d
