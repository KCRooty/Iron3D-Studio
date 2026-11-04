#pragma once

// Geometria compatible con Cinema 4D -- codigo propio de Iron3D, NO usa el
// Cineware SDK (se compila siempre, tambien en Linux y en el CI). Lo usa el
// importador de .c4d (importers/cineware) y lo podra usar cualquier otro
// importador o las primitivas parametricas de Iron3D.
//
// Convenciones de C4D (documentadas en la doc publica del SDK, ver
// research_investigacion_c4d/notas): ejes zurdos con +Y arriba, unidades en
// cm, matriz = (off, v1, v2, v3) y polygonos de 3 o 4 puntos (triangulo si
// c == d) con la cara frontal segun cross(b - a, c - a).
//
// Iron3D: ejes diestros (glm), 1 unidad = 200 cm (kCmPerUnit, igual que la
// UI) y TRS con rotacion Euler XYZ (SceneNode::localMatrix = T*Rz*Ry*Rx*S).
// El paso de uno a otro es un espejo en Z.

#include "core/MeshData.h"

#include <glm/glm.hpp>

#include <array>
#include <vector>

namespace iron3d::c4d {

constexpr double kCmPerUnit = 200.0;

// Malla tal cual la guarda C4D, en su espacio (cm, ejes zurdos).
struct PolySoup {
    std::vector<glm::dvec3> points;
    std::vector<std::array<int, 4>> polys; // triangulo si [2] == [3]

    // Anade `other` transformado por `xform` (matriz de C4D como dmat4:
    // columnas v1, v2, v3, off).
    void append(const PolySoup& other, const glm::dmat4& xform);
    bool empty() const { return polys.empty(); }
};

// Direccion de las primitivas (PRIM_AXIS en C4D: 0 +X, 1 -X, 2 +Y, 3 -Y, 4 +Z, 5 -Z).
enum class Axis { PosX = 0, NegX, PosY, NegY, PosZ, NegZ };
Axis axisFromC4D(int value);

// Primitivas reconstruidas a partir de sus parametros (medidas en cm, como
// en el Gestor de Atributos de C4D). Sin redondeos de canto (fillet): el
// importador avisa cuando el archivo los usa.
PolySoup makeCube(const glm::dvec3& size);
PolySoup makeSphere(double radius, int segments);
PolySoup makePlane(double width, double height, Axis axis);
PolySoup makeCylinder(double radius, double height, int segments, bool caps, Axis axis);
PolySoup makeCone(double topRadius, double bottomRadius, double height, int segments, bool caps, Axis axis);
PolySoup makeTorus(double ringRadius, double pipeRadius, int segments, int pipeSegments, Axis axis);
PolySoup makeDisc(double innerRadius, double outerRadius, int segments, Axis axis);
PolySoup makePyramid(const glm::dvec3& size);

// Punto de C4D -> Iron3D (espejo en Z y cm -> unidades).
glm::vec3 toIronPoint(const glm::dvec3& p);

// Malla de render de Iron3D. smoothAngleRad < 0: normales planas por cara;
// si no, cada esquina promedia las caras que comparten su punto y forman un
// angulo <= smoothAngleRad con la suya (lo que hace la etiqueta Phong).
MeshData toIronMesh(const PolySoup& soup, double smoothAngleRad);

struct TRS {
    glm::vec3 position{0.0f};
    glm::vec3 rotationDeg{0.0f};
    glm::vec3 scale{1.0f};
};
// Matriz local de un objeto de C4D -> posicion/rotacion/escala de SceneNode.
// Un cizallado (ejes no perpendiculares) no tiene equivalente en TRS y se pierde.
TRS toIronTRS(const glm::dvec3& off, const glm::dvec3& v1, const glm::dvec3& v2, const glm::dvec3& v3);

} // namespace iron3d::c4d
