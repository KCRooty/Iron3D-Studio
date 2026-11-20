#pragma once

// Booleanos de mallas (2026-11-20): Union, Sustraccion e Interseccion entre dos objetos cerrados, con el
// algoritmo clasico de arboles BSP (CSG). Trabaja sobre triangulos, parte los que atraviesa el otro objeto
// y devuelve poligonos convexos (cada uno con su `faceTris`), listos para editar. Ambas mallas tienen que
// estar en el mismo espacio y ser cerradas (sin bordes abiertos) para que el resultado sea fiable.
// Limite: cada malla se parte por el plano de sus triangulos, asi que el numero de poligonos del resultado
// crece con la complejidad de los dos objetos; `booleanMeshes` rechaza entradas de mas de 6000 triangulos.

#include "MeshData.h"

#include <string>

namespace iron3d {

enum class BoolOp { Union, Subtract, Intersect };

// A op B -> out. Devuelve false (y rellena `err`) si una malla esta vacia, es demasiado grande o el resultado queda vacio.
bool booleanMeshes(const MeshData& a, const MeshData& b, BoolOp op, MeshData& out, std::string* err = nullptr);

} // namespace iron3d
