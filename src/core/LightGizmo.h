#pragma once

// Gizmo de las luces en el visor (2026-11-20), como las de Cinema 4D: una estrellita blanca en la
// posicion de la luz (en vez de una esfera) y, segun el tipo, el cono del Foco, la linea de la Luz
// Infinita/Paralela o el rectangulo/disco de la Luz de Area. Todo son segmentos de linea en mundo
// (IRenderer::drawGizmo), sin relleno.
//
// Limites: el cono/linea tiene una longitud fija en escena (no sigue el radio de decaimiento real) y no
// hay tiradores para editar angulo o tamano arrastrando; la Luz Tubo se dibuja como linea y la
// Fotometrica como una Omni.

#include "SceneNode.h"
#include "Vertex.h"

#include <vector>

namespace iron3d {

// Radio de la estrella (unidades de escena).
constexpr float kLightStarRadius = 0.12f;

// Segmentos (pares de vertices) de todas las luces visibles bajo `root`; `selected` va resaltado.
void buildLightGizmos(const SceneNode& root, const SceneNode* selected, std::vector<Vertex>& lines);

// Segmentos de UNA luz en espacio local (la luz apunta hacia +Z, como en C4D), sin la estrella.
void lightGizmoLocalLines(const LightData& l, std::vector<glm::vec3>& segments);

// Segmentos de la estrella en espacio local, radio `r`.
void lightStarLines(float r, std::vector<glm::vec3>& segments);

} // namespace iron3d
