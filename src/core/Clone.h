#pragma once

// Clonador (2026-11-20): genera copias del primer hijo con malla en modo Lineal, Cuadricula o Radial.
// Funciona como los Deformadores y la Simetria: el nodo con CloneData dibuja la malla clonada, el hijo
// se oculta y se sigue editando aparte. No destructivo: borrar el nodo deja el hijo como estaba.
// Limite: una sola malla fusionada (no son instancias), maximo 1000 clones y 2 millones de vertices.

#include "MeshData.h"
#include "SceneNode.h"

#include <glm/glm.hpp>

namespace iron3d {

constexpr int kMaxClones = 1000;

// Numero de copias que producen los ajustes (ya recortado a kMaxClones).
int cloneCount(const CloneData& c);

// Copia `src` (llevada al espacio del nodo con `toParent`) segun los ajustes de `c`.
MeshData cloneMeshData(const MeshData& src, const CloneData& c, const glm::mat4& toParent);

// Pone al dia el mesh de cada nodo clonador (no hace nada si nada ha cambiado). Devuelve cuantos se recalcularon.
int evaluateCloners(SceneNode& root);

} // namespace iron3d
