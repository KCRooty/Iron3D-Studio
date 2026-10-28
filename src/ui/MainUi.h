#pragma once

// Paneles ImGui de la Fase 2 (Documento Maestro §8.1 punto 2): Gestor de
// Objetos interactivo de verdad sobre el grafo de escena (arbol con
// seleccion, visibilidad, anadir/eliminar) + panel de Atributos con la
// posicion/rotacion/escala del nodo seleccionado. Materiales (Fase 5): pestana
// "Textura" real + combo de asignacion en Atributos. Timeline sigue pendiente.

#include "core/Material.h"
#include "core/Preferences.h"
#include "core/SceneHistory.h"
#include "core/SceneNode.h"
#include "core/TransformGizmo.h"

#include <filesystem>
#include <vector>

namespace iron3d {

// `selected` se actualiza si el usuario hace clic en otro nodo, lo duplica,
// borra el seleccionado o carga/deshace/rehace una escena entera encima.
// `gizmoMode` se actualiza desde la barra de herramientas (Mover/Rotar/
// Escalar) y los atajos de teclado 1/2/3. `scenesDir` es donde vive
// Guardar/Cargar escena (Fase 2c). `history` es la pila de deshacer/rehacer
// (Fase 2d, Ctrl+Z/Ctrl+Y) y tambien registra los cambios hechos fuera de
// esta funcion (p.ej. el arrastre del gizmo en main.cpp). `activeBackend` es
// el backend con el que YA se arranco esta sesion (de solo lectura, para
// mostrar contraste); `prefs`/`prefsPath` son las preferencias en disco
// (Fase 3c) -- cambiar el backend ahi solo tiene efecto al reiniciar, igual
// que en Cinema 4D. `materials` (Fase 5) es la biblioteca global de la
// escena -- editable desde la pestana "Textura" del panel Crear, y asignable
// a un nodo desde Atributos > Objeto.
void drawMainUi(SceneNode& root, SceneNode*& selected, GizmoMode& gizmoMode,
                 const std::filesystem::path& scenesDir, SceneHistory& history, float framerate,
                 RendererBackend activeBackend, Preferences& prefs, const std::filesystem::path& prefsPath,
                 std::vector<Material>& materials);

} // namespace iron3d
