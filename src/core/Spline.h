#pragma once

// Splines (2026-11-24): curvas de C4D como objetos de escena. Un nodo con SplineData se dibuja como lineas en
// el visor (como las formas de los Nulos) y sirve de entrada a los generadores Extrusion, Torno, Forro,
// Recorrido, al deformador Spline, a MoText, Trazador, MoSpline y Mascara de Spline.
//
// SplineData::kind: 0 libre (lista de puntos), 1 Arco, 2 Circulo, 3 Helice, 4 n-Lados, 5 Rectangulo,
// 6 Estrella, 7 Texto, 8 4-Lados, 9 Cisoide, 10 Rueda Dentada, 11 Cicloide, 12 Formula, 13 Flor, 14 Perfil,
// 15 Vectorizador (contorno de una imagen). Las medidas van en cm; los puntos libres en unidades (1 = 200 cm).

#include "MeshData.h"
#include "SceneNode.h"
#include "Vertex.h"

#include <glm/glm.hpp>

#include <string>
#include <vector>

namespace iron3d {

constexpr int kSplineKindCount = 16;
const char* splineKindName(int kind);  // clave de traduccion

// Un contorno: polilinea en el espacio local del nodo.
struct SplineContour {
    std::vector<glm::vec3> pts;
    bool closed = false;
};

// Contornos de un nodo spline ya muestreados (curvas suavizadas, primitivas generadas...).
std::vector<SplineContour> splineContours(const SplineData& s);
// Valores por defecto de cada primitiva.
SplineData defaultSpline(int kind);

// Lineas (pares de vertices) de todos los splines visibles, en mundo; `selected` resaltado.
void buildSplineLines(const SceneNode& root, const SceneNode* selected, std::vector<Vertex>& lines);
// Seleccion por clic (como rayHitsNullShape).
bool rayHitsSpline(const SceneNode& node, const glm::mat4& world, const glm::vec3& o, const glm::vec3& d, float& dist);

// Contornos de un texto (fuente del sistema). height en unidades. Vacio si no hay fuente.
std::vector<SplineContour> textContours(const std::string& utf8, float height, float spacing = 0.0f);

// Booleanas de splines (plano XY del nodo, por rasterizado + marching squares: resultado aproximado a la
// resolucion `res`). op: 0 Union, 1 Sustraer (A-B), 2 Y (interseccion), 3 O (exclusiva), 4 Interseccion (todas
// las piezas por separado).
std::vector<SplineContour> splineBoolean(const std::vector<std::vector<SplineContour>>& inputs, int op, int res = 384);

// Triangula los contornos cerrados (en el plano XY, con agujeros por paridad) a una malla plana en z.
MeshData capMesh(const std::vector<SplineContour>& contours, float z = 0.0f, bool flip = false);

// Generadores (todo en el espacio del nodo generador).
MeshData extrudeContours(const std::vector<SplineContour>& contours, const glm::vec3& offset, int segments, bool caps);
MeshData latheContour(const SplineContour& profile, float angleDeg, int segments);
MeshData loftContours(const std::vector<SplineContour>& rings, int segmentsU, bool caps);
MeshData sweepContour(const SplineContour& profile, const SplineContour& path, float endScale, float rotateDeg, bool caps);

// Remuestrea un contorno a `count` puntos equidistantes.
SplineContour resampleContour(const SplineContour& c, int count);
float contourLength(const SplineContour& c);
// Punto y tangente a una fraccion 0..1 de la longitud.
glm::vec3 contourPointAt(const SplineContour& c, float t, glm::vec3* tangent = nullptr);
// Parte de un contorno desde `t0` hasta `t1` (0..1).
SplineContour contourPart(const SplineContour& c, float t0, float t1);

// Imagen en escala de grises 0..1 (Relieve). false si no se puede leer.
bool loadGrayImage(const std::string& path, std::vector<float>& out, int& w, int& h, int maxSide = 512);

// Contorno de una imagen (Vectorizador): umbral de luminancia 0..1. Vacio si no se puede leer.
std::vector<SplineContour> vectorizeImage(const std::string& path, float threshold, float width);

} // namespace iron3d
