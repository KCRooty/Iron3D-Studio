#pragma once

// Datos de malla en CPU (Fase 2c: mas primitivas que el cubo de la Fase 1)
// para subir a PrimitiveMesh. Con normales desde la Fase 5 (iluminacion
// basica); sin UV/texturas todavia.

#include "Vertex.h"

#include <cstdint>
#include <vector>

namespace iron3d {

struct MeshData {
    std::vector<Vertex> vertices;
    std::vector<uint16_t> indices;
};

MeshData makeCubeMesh();
MeshData makeSphereMesh(int rings = 12, int segments = 18);
MeshData makePlaneMesh();

} // namespace iron3d
