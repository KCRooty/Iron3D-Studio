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

#include <memory>
#include <string>
#include <vector>

namespace iron3d {

class SceneNode {
public:
    explicit SceneNode(std::string name, bool hasMesh = false);

    SceneNode* addChild(std::string name, bool hasMesh = false);
    // Borra `child` de este nodo si es hijo directo suyo. Devuelve true si lo encontro.
    bool removeChild(SceneNode* child);

    glm::mat4 localMatrix() const;

    int id() const { return id_; }
    const std::string& name() const { return name_; }
    void setName(std::string name) { name_ = std::move(name); }

    SceneNode* parent() const { return parent_; }
    const std::vector<std::unique_ptr<SceneNode>>& children() const { return children_; }

    glm::vec3 position{0.0f, 0.0f, 0.0f};
    glm::vec3 rotationDeg{0.0f, 0.0f, 0.0f}; // Euler XYZ en grados, solo para la UI
    glm::vec3 scale{1.0f, 1.0f, 1.0f};
    bool visible = true;
    bool hasMesh = false; // si true, el bucle principal dibuja el cubo comun en este nodo

private:
    int id_;
    std::string name_;
    SceneNode* parent_ = nullptr;
    std::vector<std::unique_ptr<SceneNode>> children_;
};

} // namespace iron3d
