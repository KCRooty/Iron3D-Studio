#pragma once

// Grafo de escena (Fase 2, Documento Maestro §8.1 punto 2): arbol de nodos
// con transformacion local (posicion/rotacion/escala) y jerarquia
// padre-hijo, igual que cualquier DCC -- diseno propio desde cero, sin
// mirar el formato interno de Maxon (ver AGENTS.md). El orden de rotacion
// es XYZ simple, intencionadamente distinto del HPB documentado de C4D:
// no se busca igualar su convencion exacta, solo tener una jerarquia TRS
// funcional para el Gestor de Objetos.

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "MeshData.h"

#include <memory>
#include <string>
#include <vector>

namespace iron3d {

// Tipo de primitiva del nodo (Fase 2c): que malla comun dibuja el bucle
// principal en este nodo, si alguna. `Light` no ilumina nada todavia (no hay
// pipeline PBR hasta una fase posterior) -- es solo un icono organizativo de
// tamano fijo, ver drawScene() en main.cpp.
// `Mesh` (2026-11-04) = Objeto Poligonal de C4D: geometria propia en
// SceneNode::mesh (la que llega al importar un .c4d).
enum class PrimitiveType { None, Cube, Sphere, Plane, Light, Mesh };
const char* primitiveTypeName(PrimitiveType type); // nombre para la UI, en espanol

// Estado de visibilidad de C4D: Por Defecto (gris, hereda del padre),
// Activado (verde, visible aunque el padre este oculto) o Apagado (rojo).
enum class VisMode { Default = 0, On = 1, Off = 2 };
inline bool resolveVisibility(VisMode mode, bool parentVisible) {
    return mode == VisMode::On ? true : (mode == VisMode::Off ? false : parentVisible);
}

class SceneNode {
public:
    explicit SceneNode(std::string name, PrimitiveType primitive = PrimitiveType::None);

    SceneNode* addChild(std::string name, PrimitiveType primitive = PrimitiveType::None);
    // Borra `child` de este nodo si es hijo directo suyo. Devuelve true si lo encontro.
    bool removeChild(SceneNode* child);
    void clearChildren() { children_.clear(); }
    // Mueve todos los hijos de `other` a este nodo (deja `other` sin hijos) y
    // corrige su `parent_` -- usado por el cargador de escenas (SceneSerializer),
    // que arma el arbol nuevo en un nodo temporal antes de aplicarlo de verdad
    // a la raiz real (si el parseo falla a medias, la raiz real no se toca).
    void adoptChildrenFrom(SceneNode& other);
    // Como adoptChildrenFrom, pero conserva los hijos que ya hubiera (los de
    // `other` van detras) -- usado por Fusionar / importar un .c4d.
    void appendChildrenFrom(SceneNode& other);

    glm::mat4 localMatrix() const;
    // Acumula localMatrix() con la de todos los ancestros -- usado por el
    // gizmo de transformacion (Fase 2b) para saber donde dibujarse en mundo
    // aunque el nodo este anidado bajo otros con su propia transformacion.
    glm::mat4 worldMatrix() const;
    glm::mat4 parentWorldMatrix() const;

    int id() const { return id_; }
    const std::string& name() const { return name_; }
    void setName(std::string name) { name_ = std::move(name); }

    SceneNode* parent() const { return parent_; }
    const std::vector<std::unique_ptr<SceneNode>>& children() const { return children_; }

    glm::vec3 position{0.0f, 0.0f, 0.0f};
    glm::vec3 rotationDeg{0.0f, 0.0f, 0.0f}; // Euler XYZ en grados, solo para la UI
    glm::vec3 scale{1.0f, 1.0f, 1.0f};
    // Visibilidad como en C4D: dos interruptores independientes (los dos
    // puntos del Gestor de Objetos), el de arriba para el visor (editor) y
    // el de abajo para el render. Por Defecto hereda del padre.
    VisMode editorVis = VisMode::Default;
    VisMode renderVis = VisMode::Default;
    PrimitiveType primitive = PrimitiveType::None;
    // Indice en el std::vector<Material> global (ver core/Material.h); -1 =
    // sin material asignado (usa Material::defaultMaterial() al dibujar).
    int materialIndex = -1;
    // Solo con primitive == Mesh. Inmutable y compartida: Duplicar y los
    // pasos de deshacer reutilizan la misma malla sin copiarla; editarla
    // (cuando haya modelado) sera crear una nueva y cambiar el puntero.
    std::shared_ptr<const MeshData> mesh;

private:
    int id_;
    std::string name_;
    SceneNode* parent_ = nullptr;
    std::vector<std::unique_ptr<SceneNode>> children_;
};

// Clona `src` (con toda su jerarquia) como hijo nuevo de `parent` -- usado
// por "Duplicar" en el Gestor de Objetos (Fase 2d). Devuelve el nodo raiz de
// la copia (el de nivel superior, ya seleccionable). Los hijos clonados
// conservan su nombre tal cual; solo el nodo de nivel superior lleva el
// sufijo " copia", para distinguirlo del original en la lista.
SceneNode* duplicateSubtree(SceneNode& parent, const SceneNode& src);

} // namespace iron3d
