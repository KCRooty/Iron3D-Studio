#pragma once

// Paneles ImGui de la Fase 2 (Documento Maestro §8.1 punto 2): Gestor de
// Objetos interactivo de verdad sobre el grafo de escena (arbol con
// seleccion, visibilidad, anadir/eliminar) + panel de Atributos con la
// posicion/rotacion/escala del nodo seleccionado. Materiales y Timeline
// siguen pendientes de fases posteriores.

#include "core/SceneNode.h"
#include "core/TransformGizmo.h"

#include <filesystem>

namespace iron3d {

// `selected` se actualiza si el usuario hace clic en otro nodo, borra el
// seleccionado o carga una escena nueva encima. `gizmoMode` se actualiza
// desde la barra de herramientas (Mover/Rotar/Escalar) y los atajos de
// teclado 1/2/3. `scenesDir` es donde vive Guardar/Cargar escena (Fase 2c).
void drawMainUi(SceneNode& root, SceneNode*& selected, GizmoMode& gizmoMode,
                 const std::filesystem::path& scenesDir, float framerate);

} // namespace iron3d
