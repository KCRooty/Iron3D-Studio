#pragma once

// Objeto Simetria (2026-11-19), el generador de Modelado de C4D: un nodo con
// SymmetryData cuyo primer hijo con malla se refleja respecto a un plano del
// propio nodo. El hijo se queda como esta (editable y visible); el nodo
// Simetria dibuja SOLO la copia reflejada, asi hijo + nodo forman el conjunto
// y cualquier cambio del hijo (mover un punto, animarlo) se ve al instante.
// La otra simetria de Iron3D es la de edicion (Malla > Simetria de edicion):
// esa mueve puntos espejo dentro de una misma malla, no crea objetos.

#include "MeshData.h"
#include "SceneNode.h"

#include <glm/glm.hpp>

namespace iron3d {

// Copia reflejada de `src` (en el espacio de su nodo) respecto al plano `axis`
// (0 = X, 1 = Y, 2 = Z) del espacio del padre: `toParent` lleva los puntos del
// hijo a ese espacio. Invierte el giro de los triangulos y refleja las
// normales. Los puntos a menos de `tol` del plano se pegan a el, para que las
// dos mitades se toquen sin hueco.
MeshData mirrorMeshData(const MeshData& src, int axis, const glm::mat4& toParent, float tol);

// Pone al dia el mesh de cada nodo con simetria activa (no hace nada si ni el
// hijo ni el nodo han cambiado). Devuelve cuantos nodos se recalcularon.
int evaluateSymmetry(SceneNode& root);

// Los generadores (Simetria, Clonador, Deformadores) trabajan con la malla de sus hijos, y los Cubo,
// Esfera y Plano de Iron3D son parametricos sin malla propia. Esto convierte esos hijos en objetos
// poligonales con la misma malla (como Hacer Editable) para que el generador los vea. Devuelve cuantos
// ha convertido. Limite: el hijo deja de ser parametrico (para el Cubo/Esfera/Plano no cambia nada: su
// tamano ya sale de la escala del nodo).
int bakeParametricChildren(SceneNode& node);

} // namespace iron3d
