#pragma once

// Paneles ImGui de la Fase 2 (Documento Maestro §8.1 punto 2): Gestor de
// Objetos interactivo de verdad sobre el grafo de escena (arbol con
// seleccion, visibilidad, anadir/eliminar) + panel de Atributos con la
// posicion/rotacion/escala del nodo seleccionado. Materiales y Timeline
// siguen pendientes de fases posteriores.

#include "core/SceneNode.h"
#include "core/TransformGizmo.h"

namespace iron3d {

// `selected` se actualiza si el usuario hace clic en otro nodo o borra el seleccionado.
// `gizmoMode` se actualiza desde la barra de herramientas (Mover/Rotar/Escalar) y los
// atajos de teclado 1/2/3 -- el propio TransformGizmo lo usa para saber que dibujar.
void drawMainUi(SceneNode& root, SceneNode*& selected, GizmoMode& gizmoMode, float framerate);

} // namespace iron3d
