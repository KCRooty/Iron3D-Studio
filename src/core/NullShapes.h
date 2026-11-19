#pragma once

// Formas de visualizacion de los Nulos (2026-11-19): en C4D un Nulo puede mostrarse como circulo,
// rectangulo, cubo, esfera, ejes... (pestana Basico > Visualizar, ids de onull.h). Se usan mucho
// como controladores de rigs con IK. Aqui se convierten en segmentos de linea en espacio mundo para
// el visor (IRenderer::drawGizmo).

#include "SceneNode.h"
#include "Vertex.h"

#include <vector>

namespace iron3d {

// Ids de NULLOBJECT_DISPLAY (onull.h) con los que se guarda NullDisplay::shape.
enum NullShape {
    NullDot = 0, NullPoint, NullCircle, NullRectangle, NullDiamond, NullTriangle, NullPentagon,
    NullHexagon, NullOctagon, NullStar, NullAxis, NullCube, NullPyramid, NullSphere, NullNone
};

// Segmentos (pares de vertices) de las formas de todos los nulos visibles bajo `root`.
// `selected` se dibuja resaltado. Los nodos sin forma (shape < 0 o NullNone) no dibujan nada.
void buildNullShapes(const SceneNode& root, const SceneNode* selected, std::vector<Vertex>& lines);

// Ejes derecha/arriba de la camara activa (de su matriz de vista) para las formas con orientacion
// Pantalla, que miran a la camara. Llamar una vez por fotograma antes de dibujar y de elegir.
void setNullScreenAxes(const glm::mat4& view);

// Segmentos de una forma en espacio local del nodo (radio en unidades de escena).
void nullShapeLocalLines(const NullDisplay& d, std::vector<glm::vec3>& segments);

// Seleccion por clic: true si el rayo (origen `o`, direccion `d` normalizada, en mundo) pasa lo bastante
// cerca de las lineas de la forma del Nulo (matriz de mundo `world`). La tolerancia crece con la distancia
// (aprox. unos pixeles en pantalla). `dist` = distancia a lo largo del rayo hasta el punto mas cercano.
bool rayHitsNullShape(const SceneNode& node, const glm::mat4& world, const glm::vec3& o, const glm::vec3& d,
                      float& dist);

} // namespace iron3d
