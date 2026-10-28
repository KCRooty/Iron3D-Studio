#pragma once

// Deshacer/rehacer (Fase 2d) por snapshots completos del arbol de escena,
// serializados con el mismo formato .iron ya usado para guardar/cargar
// (SceneSerializer). Para el tamano de escena de este proyecto (un puñado
// de primitivas) clonar el arbol entero a texto en cada paso es mas simple
// y mas dificil de romper que un sistema de comandos/deltas por campo, y el
// coste es insignificante.
//
// Restaurar un snapshot reconstruye el arbol entero (misma mecanica que
// cargar un .iron): cualquier SceneNode* que el llamador tuviera guardado
// (como el nodo seleccionado) queda invalido tras un undo/redo -- hay que
// limpiar la seleccion despues, igual que ya hace el panel de Cargar escena.
//
// Fase 5 (materiales): el constructor toma una referencia a la lista de
// materiales de la escena y la reenvia internamente a serializeScene/
// deserializeScene en cada snapshot/undo/redo -- asi un snapshot captura
// nodo+material consistentemente (sin arrastre de indices si la lista de
// materiales cambia entre dos snapshots) SIN tener que anadir un parametro
// nuevo en los ~15 sitios que ya llaman a snapshot()/commitBefore()/undo()/
// redo(): solo el constructor cambia.

#include "SceneNode.h"
#include "Material.h"

#include <string>
#include <vector>

namespace iron3d {

class SceneHistory {
public:
    explicit SceneHistory(std::vector<Material>& materials) : materials_(materials) {}

    void reset() {
        undoStack_.clear();
        redoStack_.clear();
    }

    // Snapshot de `root` (+ materiales) tal cual estan AHORA, sin tocar
    // ninguna pila -- para ediciones que duran varios frames (arrastrar un
    // campo, escribir un nombre): se toma uno al activarse el campo y se
    // guarda aparte hasta que el campo se suelta, momento en el que se pasa
    // a commit().
    std::string snapshot(const SceneNode& root) const;

    // Empuja un snapshot ya tomado (de ANTES del cambio) a la pila de
    // deshacer y vacia la de rehacer, como cualquier editor: un cambio
    // nuevo invalida el "rehacer" de una rama vieja.
    void commit(std::string preChangeSnapshot);

    // Snapshot-y-commit en un solo paso, para cambios instantaneos (anadir,
    // eliminar, duplicar) donde no hace falta separar activar/confirmar.
    void commitBefore(const SceneNode& root) { commit(snapshot(root)); }

    bool canUndo() const { return !undoStack_.empty(); }
    bool canRedo() const { return !redoStack_.empty(); }

    // Reconstruye `root` (+ materiales) con el snapshot anterior/siguiente.
    // Devuelve false (sin tocar nada) si no hay nada que deshacer/rehacer.
    bool undo(SceneNode& root);
    bool redo(SceneNode& root);

private:
    std::vector<Material>& materials_;
    std::vector<std::string> undoStack_;
    std::vector<std::string> redoStack_;
    static constexpr size_t kMaxDepth = 100; // suficiente para una sesion de edicion normal
};

} // namespace iron3d
