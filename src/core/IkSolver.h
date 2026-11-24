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
// Con hermanos del mismo nombre (muy comun en rigs de C4D) el 2.o, 3.o... llevan el sufijo "~#2", "~#3":
// "Brazo/Hueso~#2" es el segundo hijo de "Brazo" llamado "Hueso".
SceneNode* findNodeByPath(SceneNode& anyNode, const std::string& path);

// (childByPathSegment / pathSegmentOf / pathSegmentFor, en SceneNode.h, son las piezas de estas rutas.)

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

// Aplica los limites de rotacion (SceneNode::jointLimits) de `node` y sus descendientes:
// cada eje activo se recorta a [min, max] (mezclado por la fuerza). Devuelve cuantos ejes
// se recortaron. Los hijos conservan su rotacion local, asi que la punta se mueve.
int applyJointLimits(SceneNode& node);

// Resuelve todas las etiquetas IK activas del arbol (las que tienen `tip` y `target` validos).
// Devuelve cuantas cadenas se resolvieron.
int solveAllIk(SceneNode& root);

// Etiqueta Proteccion/Limite (SceneNode::protection) tras una edicion del usuario (gizmo, campos de
// Coordenadas): los ejes en modo Bloquear vuelven a su valor `before*` y los de modo Limite se recortan a
// [min, max]. `before*` = position, rotationDeg y scale locales antes de editar. Devuelve cuantos ejes
// se corrigieron.
int applyProtection(SceneNode& node, const glm::vec3& beforePos, const glm::vec3& beforeRot,
                    const glm::vec3& beforeScl);

// Recorta los ejes en modo Limite de las etiquetas con "Permitir expresiones" desactivado, que tambien
// limitan a la IK; las de expresiones permitidas solo limitan la edicion manual. Los nodos con pistas de
// animacion en un canal se saltan en ese canal (manda la animacion). Devuelve cuantos ejes se recortaron.
int clampProtectionLimits(SceneNode& root);

} // namespace iron3d
