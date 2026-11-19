#pragma once

// Piel (skinning) en vivo (2026-11-19): la malla sigue a las articulaciones.
// Metodo publico (linear blend skinning): cada vertice guarda hasta 4
// articulaciones con su peso; lo que no suma 1 se queda rigido con la malla.
// La malla "de bind" es la del archivo (en la pose que guardo C4D); al mover
// las articulaciones, evaluateSkins() crea una malla deformada y la pone en
// SceneNode::mesh. Si alguien edita la malla (modelado) la Piel se descarta.

#include "MeshData.h"

#include <glm/glm.hpp>

#include <memory>
#include <string>
#include <vector>

namespace iron3d {

class SceneNode;

struct SkinInfluence {
    int joint[4] = {-1, -1, -1, -1}; // indice en SkinData::jointPaths
    float weight[4] = {0, 0, 0, 0};  // suma <= 1; el resto sigue rigido
};

struct SkinData {
    std::vector<std::string> jointPaths;    // ruta por nombres desde la raiz de la escena
    std::vector<SkinInfluence> influences;  // una por vertice de bindMesh
    std::shared_ptr<const MeshData> bindMesh;

    // Los rellena bindSkins (pose de la escena en ese momento = pose de bind).
    bool bound = false;
    glm::mat4 meshBindWorld{1.0f};
    std::vector<glm::mat4> jointInvBind;

    // Cache de evaluateSkins: no se rehace la malla si nada se ha movido.
    std::vector<glm::mat4> lastJoint;
    glm::mat4 lastMeshWorld{1.0f};
    std::shared_ptr<const MeshData> output;
};

// Fija la pose actual de la escena como pose de bind en todas las Pieles sin vincular.
// Devuelve cuantas se vincularon (las que no encuentran alguna articulacion se descartan).
int bindSkins(SceneNode& root);

// Deforma las mallas con Piel segun las articulaciones actuales. Devuelve cuantas mallas se rehicieron.
int evaluateSkins(SceneNode& root);

} // namespace iron3d
