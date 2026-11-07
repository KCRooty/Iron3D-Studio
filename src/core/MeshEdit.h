#pragma once

// Herramientas de modelado sobre poligonos (2026-11-07): Extruir, Biselar
// (insercion + extrusion de cada poligono) y Cuchillo en modo Linea. Trabajan
// sobre la topologia de MeshTopology y devuelven una malla nueva con
// faceTris explicito; la de entrada no se toca (asi se puede previsualizar
// mientras se arrastra y deshacer con la historia de siempre).

#include "MeshData.h"
#include "MeshTopology.h"

#include <glm/glm.hpp>

#include <functional>
#include <set>
#include <vector>

namespace iron3d {

struct MeshEditResult {
    MeshData mesh;
    std::vector<int> selectTris;  // triangulos que forman la seleccion nueva (para pasarla a poligonos)
    int changed = 0;              // poligonos afectados (0 = no se hizo nada)
};

// Extruir (D): los poligonos seleccionados se separan como una region y se
// desplazan `distance` por la normal media de cada punto; en el borde de la
// region salen paredes. La seleccion nueva son las tapas.
MeshEditResult extrudePolygons(const MeshData& mesh, const MeshTopology& topo, const std::set<int>& polys, float distance);

// Biselar en modo Poligonos: cada poligono seleccionado se encoge hacia su
// centro `inset` (distancia por esquina, sin pasar del 95 %) y se desplaza
// `extrusion` por su normal; entre el borde original y el nuevo sale un anillo
// de cuadrilateros. La seleccion nueva son los poligonos interiores.
MeshEditResult bevelPolygons(const MeshData& mesh, const MeshTopology& topo, const std::set<int>& polys, float inset,
                             float extrusion);

// Cuchillo en modo Linea: corta por el plano (planePoint, planeNormal), en
// espacio local, los poligonos que cruza. Solo se corta un poligono si
// `allowed` lo deja (puede ser vacio = todos) y si los dos puntos de corte
// pasan `inRange` (el tramo dibujado en pantalla). Los poligonos tienen que
// ser convexos (lo son los de Iron3D).
MeshEditResult knifeCut(const MeshData& mesh, const MeshTopology& topo, const glm::vec3& planePoint,
                        const glm::vec3& planeNormal, const std::vector<char>& allowed,
                        const std::function<bool(const glm::vec3&)>& inRange);

// Seleccion de poligonos a partir de triangulos (tras rehacer la topologia).
std::set<int> polysFromTris(const MeshTopology& topo, const std::vector<int>& tris);

} // namespace iron3d
