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

// Cuchillo en modo Bucle: corta todo el anillo de aristas de `edge` (las opuestas de cuadrilatero en
// cuadrilatero) por el punto t (0..1, medido desde el extremo de menor indice de cada arista; 0.5 = mitad).
// Cada cuadrilatero del anillo pasa a dos. changed = cuadrilateros partidos (0 = nada que cortar).
MeshEditResult loopCut(const MeshData& mesh, const MeshTopology& topo, int edge, float t = 0.5f);

// Seleccion de poligonos a partir de triangulos (tras rehacer la topologia).
std::set<int> polysFromTris(const MeshTopology& topo, const std::vector<int>& tris);

// --- Seleccion por topologia (indices de la topologia; mode 1/2/3 = puntos/aristas/poligonos)
// Bucle de aristas: sigue la arista por puntos de 4 aristas (o por el borde
// abierto) hasta cerrarse o llegar a un polo.
std::set<int> edgeLoop(const MeshTopology& topo, int edge);
// Anillo de aristas: las aristas opuestas de cuadrilatero en cuadrilatero.
std::set<int> edgeRing(const MeshTopology& topo, int edge);
// Aumentar / Reducir / Seleccionar Conectados. Vecinos: puntos por arista;
// aristas y poligonos por punto comun.
std::set<int> growSelection(const MeshTopology& topo, int mode, const std::set<int>& sel);
std::set<int> shrinkSelection(const MeshTopology& topo, int mode, const std::set<int>& sel);
// Seleccionar Patron: de la seleccion actual, ordenada por indice, se queda con 1 de cada `step` empezando
// por la posicion `offset` (0 = la primera, 1 = la segunda...). step < 2 deja la seleccion como esta.
// Limite: el orden es el de los indices de la topologia, no el recorrido geometrico de un bucle.
std::set<int> patternSelection(const std::set<int>& sel, int step, int offset);
// Contorno: de la seleccion, solo los elementos del borde (los que tocan algo sin seleccionar).
std::set<int> outlineSelection(const MeshTopology& topo, int mode, const std::set<int>& sel);
// Rellenar (modo Poligonos): los huecos de poligonos sin seleccionar rodeados por la seleccion se seleccionan.
// Un hueco es cada grupo de poligonos sin seleccionar unidos por aristas; el mayor se considera el "fuera" y no se rellena.
std::set<int> fillSelection(const MeshTopology& topo, const std::set<int>& sel);
std::set<int> connectedSelection(const MeshTopology& topo, int mode, const std::set<int>& sel);

// --- Operaciones de malla
// Soldar: los puntos se juntan en su centro; las caras que se quedan con menos
// de 3 puntos desaparecen.
MeshEditResult weldPoints(const MeshData& mesh, const MeshTopology& topo, const std::set<int>& points);
// Cerrar Agujero: un poligono nuevo por cada borde abierto cerrado que toque
// `touchPoints` (vacio = todos). El poligono se triangula en abanico: bien
// para agujeros convexos.
MeshEditResult closeHoles(const MeshData& mesh, const MeshTopology& topo, const std::set<int>& touchPoints);
// Subdividir (lineal, sin suavizar): cada poligono de n lados se parte en n
// cuadrilateros por los puntos medios y el centro. Los vecinos sin
// seleccionar reciben el punto medio para que no queden puntos en T.
// `polys` vacio = todos.
MeshEditResult subdividePolygons(const MeshData& mesh, const MeshTopology& topo, const std::set<int>& polys);
// Invertir Normales de los poligonos indicados.
MeshEditResult flipPolygons(const MeshData& mesh, const MeshTopology& topo, const std::set<int>& polys);
// Triangular: cada poligono de mas de 3 lados pasa a n-2 triangulos (abanico). La seleccion nueva son los triangulos.
MeshEditResult triangulatePolygons(const MeshData& mesh, const MeshTopology& topo, const std::set<int>& polys);
// Destriangular: une pares de triangulos vecinos en cuadrilateros cuando sus normales forman menos de `maxAngleDeg`
// grados y el cuadrilatero resultante es convexo. Cada triangulo entra en un solo par. La seleccion nueva son los
// cuadrilateros. `polys` vacio = todos. changed = cuadrilateros creados (0 = nada que unir).
MeshEditResult untriangulatePolygons(const MeshData& mesh, const MeshTopology& topo, const std::set<int>& polys,
                                     float maxAngleDeg = 5.0f);
// Alinear Normales: dentro de cada isla conectada de `polys`, todos los poligonos quedan orientados como el primero
// (invierte los que recorren una arista compartida en el mismo sentido). changed = 0 si ya estaba todo alineado.
MeshEditResult alignNormals(const MeshData& mesh, const MeshTopology& topo, const std::set<int>& polys);

// Puente (M~B): une dos poligonos con el mismo numero de lados con un anillo de
// cuadrilateros y quita los dos originales. Prueba las dos direcciones y todos los giros
// y se queda con el emparejamiento de puntos mas corto. changed = 0 si no se puede
// (mismo poligono, distinto numero de lados, indices fuera de rango).
MeshEditResult bridgePolygons(const MeshData& mesh, const MeshTopology& topo, int polyA, int polyB);

// Comprobar Malla: problemas de la topologia (indices de MeshTopology).
struct MeshCheck {
    int openEdges = 0;          // aristas con un solo poligono (borde abierto)
    int nonManifoldEdges = 0;   // aristas con mas de dos poligonos
    int loosePoints = 0;        // puntos que no usa ningun poligono
    int degeneratePolys = 0;    // poligonos sin area
    std::set<int> openEdgeSet, nonManifoldEdgeSet, loosePointSet, degeneratePolySet;
    bool clean() const { return openEdges == 0 && nonManifoldEdges == 0 && loosePoints == 0 && degeneratePolys == 0; }
};
MeshCheck checkMesh(const MeshTopology& topo);

} // namespace iron3d
