#pragma once

// Paneles ImGui de la Fase 2 (Documento Maestro §8.1 punto 2): Gestor de
// Objetos interactivo de verdad sobre el grafo de escena (arbol con
// seleccion, visibilidad, anadir/eliminar) + panel de Atributos con la
// posicion/rotacion/escala del nodo seleccionado. Materiales y Timeline
// siguen pendientes de fases posteriores.

#include "core/SceneNode.h"

namespace iron3d {

// `selected` se actualiza si el usuario hace clic en otro nodo o borra el seleccionado.
void drawMainUi(SceneNode& root, SceneNode*& selected, float framerate);

} // namespace iron3d
