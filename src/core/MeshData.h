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
    // Poligonos (2026-11-07): cuantos triangulos consecutivos del indice forma
    // cada cara (1 = triangulo, 2 = cuadrilatero, n-2 = n-gono). Vacio = se
    // deducen (MeshTopology). Lo rellenan las herramientas de modelado para
    // que un corte o una extrusion no se vuelvan a juntar mal.
    std::vector<uint32_t> faceTris;
};

void updateBounds(MeshData& mesh);

MeshData makeCubeMesh();
MeshData makeSphereMesh(int rings = 12, int segments = 18);
MeshData makePlaneMesh();

// Primitivas poligonales (PrimitiveShapes.cpp): cuadrilateros en los lados, n-gonos en las tapas.
MeshData makeCylinderMesh(float radius = 0.5f, float height = 1.0f, int seg = 24);
MeshData makeConeMesh(float radius = 0.5f, float height = 1.0f, int seg = 24);
MeshData makeDiscMesh(float outer = 0.5f, float inner = 0.0f, int seg = 32);
MeshData makeTorusMesh(float ringRadius = 0.4f, float pipeRadius = 0.1f, int ringSeg = 32, int pipeSeg = 12);
MeshData makeCapsuleMesh(float radius = 0.25f, float height = 1.0f, int seg = 24, int capRings = 6);
MeshData makeTubeMesh(float inner = 0.3f, float outer = 0.5f, float height = 1.0f, int seg = 24);
MeshData makePyramidMesh(float width = 1.0f, float depth = 1.0f, float height = 1.0f);
// kind: 0 tetraedro, 1 octaedro, 2 icosaedro, 3 dodecaedro
MeshData makePlatonicMesh(int kind = 0, float radius = 0.5f);

} // namespace iron3d
