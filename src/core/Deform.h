#pragma once

// Deformadores (2026-11-20): Doblar, Enroscar, Afilar, Hinchar y Sesgar. Funcionan como el objeto
// Simetria: un nodo con DeformData cuyo primer hijo con malla se deforma; el nodo dibuja la copia
// deformada y el hijo se oculta (queda en el Gestor de Objetos para editarlo). Cambiar el hijo o
// cualquier parametro se ve al instante. No destructivo: borrar el nodo deja el hijo como estaba.

#include "MeshData.h"
#include "SceneNode.h"

#include <glm/glm.hpp>

namespace iron3d {

// Nombres de `kind`: 0 Doblar, 1 Enroscar, 2 Afilar, 3 Hinchar, 4 Sesgar.
// `axis` (0 X, 1 Y, 2 Z del nodo) es el eje a lo largo del cual actua. `amount` va en grados
// (Doblar, Enroscar) o en porcentaje (Afilar, Hinchar, Sesgar). `toParent` lleva los puntos del hijo
// al espacio del nodo deformador.
MeshData deformMeshData(const MeshData& src, int kind, int axis, float amount, const glm::mat4& toParent);

// Pone al dia el mesh de cada nodo deformador (no hace nada si nada ha cambiado). Devuelve cuantos se recalcularon.
int evaluateDeformers(SceneNode& root);

} // namespace iron3d
