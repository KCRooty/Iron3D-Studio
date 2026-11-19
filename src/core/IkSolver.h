#pragma once

// Solver de cinematica inversa (IK) para cadenas de articulaciones (2026-11-19).
// FABRIK sobre las posiciones en mundo (conserva la longitud de cada hueso) y
// despues se giran las articulaciones para que cada una apunte al siguiente
// punto. Funciona con la jerarquia de SceneNode: la etiqueta IK (IkTagData) vive
// en la raiz de la cadena y `tip` es el final.

#include "SceneNode.h"

#include <glm/glm.hpp>

#include <optional>
#include <string>
#include <vector>

namespace iron3d {

// Ruta por nombres desde la raiz de la escena ("Grupo/Cubo") -> nodo (nullptr si no existe).
SceneNode* findNodeByPath(SceneNode& anyNode, const std::string& path);

// Cadena desde `root` hasta `tip` (ambos incluidos), o vacia si `tip` no cuelga de `root`.
std::vector<SceneNode*> chainBetween(SceneNode& root, SceneNode& tip);

struct IkResult {
    bool solved = false;      // la punta llega al objetivo (error <= tolerancia)
    float error = 0.0f;       // distancia final entre la punta y el objetivo
    int iterations = 0;
};

// Mueve la cadena (raiz -> punta) para que la punta alcance `target` (mundo). La raiz no se
// traslada. `pole` (mundo) orienta el plano de la cadena (rodilla/codo); vacio = sin polo.
IkResult solveIkChain(const std::vector<SceneNode*>& chain, const glm::vec3& target,
                      const std::optional<glm::vec3>& pole = std::nullopt, int maxIterations = 24,
                      float tolerance = 1e-3f);

// Resuelve todas las etiquetas IK activas del arbol (las que tienen `tip` y `target` validos).
// Devuelve cuantas cadenas se resolvieron.
int solveAllIk(SceneNode& root);

} // namespace iron3d
