#pragma once

// Topologia de una malla editable vista como en C4D (2026-11-07): puntos
// (vertices de render unidos por posicion), poligonos (un triangulo o una cara
// de varios triangulos: cuadrilateros y n-gonos convexos) y aristas (solo el
// borde de cada poligono, sin las diagonales de la triangulacion).
//
// Las caras salen de MeshData::faceTris si la malla lo trae; si no, se
// deducen: dos triangulos consecutivos que comparten arista, estan en el mismo
// plano y forman un cuadrilatero convexo son un solo poligono (igual que al
// exportar a .c4d).

#include "MeshData.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <utility>
#include <vector>

namespace iron3d {

struct MeshTopology {
    std::vector<glm::vec3> points;
    std::vector<int> vertexPoint;            // vertice de render -> punto
    std::vector<std::vector<int>> polys;     // contorno en puntos, girando segun la normal visible
    std::vector<glm::vec3> polyNormals;      // unitarias
    std::vector<std::vector<int>> polyTris;  // triangulos de cada poligono (t = indices[3t..3t+2])
    std::vector<int> triPoly;                // triangulo -> poligono (-1 si es degenerado)
    std::vector<std::pair<int, int>> edges;  // (menor, mayor)
};

MeshTopology buildMeshTopology(const MeshData& mesh);

// Triangulos por cara, consecutivos en el indice: los de la malla
// (faceTris) si cuadran con el numero de triangulos, o los deducidos.
std::vector<uint32_t> faceTriCounts(const MeshData& mesh);

glm::vec3 polyCenter(const MeshTopology& topo, int poly);

// Tras mover vertices sin cambiar la conectividad: puntos y normales de
// poligono al dia (la seleccion sigue valiendo).
void refreshTopologyPositions(MeshTopology& topo, const MeshData& mesh);

// Malla con solo los poligonos para los que keep(p) es true, con faceTris
// explicito y sin vertices que ya no use nadie.
MeshData keepPolygons(const MeshData& mesh, const MeshTopology& topo, const std::vector<char>& keep);

// Quita los vertices que no usa ningun triangulo (renumera los indices).
void removeUnusedVertices(MeshData& mesh);

// Visibilidad de los componentes desde una camara: z-buffer en CPU con los
// triangulos de la propia malla (a la mitad de resolucion). `mvp` =
// proyeccion * vista * mundo del objeto; width/height = visor en pixeles. Un
// componente se ve si su punto de muestra (el punto, el centro de la arista o
// del poligono) no queda detras de la superficie (con un margen de un pixel
// y de un 1 % de profundidad, para que lo que esta EN la superficie cuente
// como visible). Los triangulos que cruzan el plano de la camara no tapan.
struct ComponentVisibility {
    std::vector<char> points, edges, polys;
};
ComponentVisibility computeComponentVisibility(const MeshData& mesh, const MeshTopology& topo, const glm::mat4& mvp,
                                               int width, int height);

} // namespace iron3d
