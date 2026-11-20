#pragma once

// Clonador (2026-11-20): genera copias del primer hijo con malla en modo Lineal, Cuadricula o Radial.
// Funciona como los Deformadores y la Simetria: el nodo con CloneData dibuja la malla clonada, el hijo
// se oculta y se sigue editando aparte. No destructivo: borrar el nodo deja el hijo como estaba.
// Limite: una sola malla fusionada (no son instancias), maximo 1000 clones y 2 millones de vertices.

#include "MeshData.h"
#include "SceneNode.h"

#include <glm/glm.hpp>

#include <vector>

namespace iron3d {

constexpr int kMaxClones = 1000;

// Numero de copias que producen los ajustes (ya recortado a kMaxClones).
int cloneCount(const CloneData& c);

// Puntos (sin repetir) de una malla, llevados al espacio del nodo clonador; sirven de destinos del modo Objeto.
std::vector<glm::vec3> cloneTargetPoints(const MeshData& m, const glm::mat4& toParent);

// Copia `src` (llevada al espacio del nodo con `toParent`) segun los ajustes de `c`. `points` solo se usa en el
// modo Objeto (una copia por punto, hasta kMaxClones); sin puntos en ese modo no hay copias.
MeshData cloneMeshData(const MeshData& src, const CloneData& c, const glm::mat4& toParent,
                       const std::vector<glm::vec3>* points = nullptr);

// Pone al dia el mesh de cada nodo clonador (no hace nada si nada ha cambiado). Devuelve cuantos se recalcularon.
int evaluateCloners(SceneNode& root);

} // namespace iron3d
