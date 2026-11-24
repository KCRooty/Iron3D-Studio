#pragma once

// Datos de malla en CPU (Fase 2c: mas primitivas que el cubo de la Fase 1)
// para subir a PrimitiveMesh. Con normales desde la Fase 5 (iluminacion
// basica); sin UV/texturas todavia.
//
// Indices de 32 bits (2026-11-04): las mallas importadas de .c4d pasan
// facilmente de los 65 535 vertices que permitia uint16.

#include "Vertex.h"

#include <cstdint>
#include <memory>
#include <string>
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

// Parametros de una primitiva poligonal (2026-11-20). El nodo sigue siendo un Objeto poligonal; mientras su
// malla sea la que genero estos parametros (`generated`), el panel de Atributos deja editarlos y la
// regenera. En cuanto se edita la malla, el puntero deja de coincidir y se vuelve a un objeto normal.
// No se guarda en la escena (al reabrir, la malla queda como objeto poligonal sin parametros).
struct ParamShape {
    // 0 ninguno, 1 Cilindro, 2 Cono, 3 Disco, 4 Toroide, 5 Capsula, 6 Tubo, 7 Piramide, 8 Platonico,
    // 9 Cubo, 10 Esfera, 11 Plano, 12 Tanque de Petroleo, 13 Figura, 14 Terreno Fractal, 15 Relieve, 16 Poligono
    int kind = 0;
    float a = 100.0f, b = 100.0f, c = 100.0f;  // medidas en cm (ver paramShapeLabels)
    int seg1 = 24, seg2 = 12, seg3 = 1;
    std::string image;                                  // Relieve: ruta de la imagen
    std::shared_ptr<const std::vector<float>> heights;  // Relieve: alturas 0..1 de la imagen (hw x hh)
    int hw = 0, hh = 0;
    std::shared_ptr<const MeshData> generated;
};
ParamShape defaultParamShape(int kind);
MeshData buildParamShape(const ParamShape& p);  // la malla en unidades (1 = 200 cm)

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
// 2026-11-24: Cubo, Esfera y Plano con medidas y segmentos, y los objetos que faltaban.
MeshData makeBoxMesh(float w, float h, float d, int sx, int sy, int sz);
MeshData makeGridPlaneMesh(float w, float d, int sw, int sd, int orient);  // orient 0 +Y, 1 -Y, 2 +X, 3 -X, 4 +Z, 5 -Z
MeshData makeOilTankMesh(float radius, float height, float capHeight, int seg);
MeshData makeFigureMesh(float height);
MeshData makeLandscapeMesh(float w, float d, float h, int sw, int sd, int seed);
MeshData makeReliefMesh(const std::vector<float>* heights, int hw, int hh, float w, float d, float h, int sw, int sd);
MeshData makePolygonMesh(float w, float h, bool triangle);

} // namespace iron3d
