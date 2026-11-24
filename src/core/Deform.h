#pragma once

// Deformadores (2026-11-20): Doblar, Enroscar, Afilar, Hinchar y Sesgar. Funcionan como el objeto
// Simetria: un nodo con DeformData cuyo primer hijo con malla se deforma; el nodo dibuja la copia
// deformada y el hijo se oculta (queda en el Gestor de Objetos para editarlo). Cambiar el hijo o
// cualquier parametro se ve al instante. No destructivo: borrar el nodo deja el hijo como estaba.

#include "MeshData.h"
#include "SceneNode.h"

#include <glm/glm.hpp>

namespace iron3d {

// Nombres de `kind`: 0 Doblar, 1 Enroscar, 2 Afilar, 3 Hinchar, 4 Sesgar, 5 Aplastar y Estirar, 6 Esferizar,
// 7 Derretir, 8 Subdivision Superficie (Catmull-Clark; `amount` = numero de subdivisiones, 1 a 4).
// `axis` (0 X, 1 Y, 2 Z del nodo) es el eje a lo largo del cual actua. `amount` va en grados
// (Doblar, Enroscar) o en porcentaje (Afilar, Hinchar, Sesgar). `toParent` lleva los puntos del hijo
// al espacio del nodo deformador.
// Datos extra de algunos tipos: Morph (malla destino y su matriz) y FFD (8 esquinas).
struct DeformExtra {
    const MeshData* second = nullptr;
    glm::mat4 secondMatrix{1.0f};
    const glm::vec3* ffd = nullptr;  // 8 desplazamientos, fraccion del tamano de la caja
};
MeshData deformMeshData(const MeshData& src, int kind, int axis, float amount, const glm::mat4& toParent,
                        const DeformExtra* extra = nullptr);

// Subdivision Catmull-Clark: cada poligono de n lados pasa a n cuadrilateros y la malla se suaviza
// (los bordes abiertos se quedan como curva, no se encogen). Normales suaves; conserva color y UV de los puntos.
MeshData subdivideCatmullClark(const MeshData& src, int levels);

// Ruta por nombres desde la raiz ("Grupo/Cubo") de un nodo (la inversa es findNodeByPath, de IkSolver.h).
std::string nodePathFromRoot(const SceneNode& root, const SceneNode& node);

// Pone al dia el mesh de cada nodo deformador (no hace nada si nada ha cambiado). Devuelve cuantos se recalcularon.
// Fotograma y FPS para los tipos que dependen del tiempo (Jiggle, Formula, Viento, MoInstance).
void setDeformTime(float frame, float fps);
int evaluateDeformers(SceneNode& root);

// Pone al dia las Instancias (comparten la malla de su objeto de origen).
void evaluateInstances(SceneNode& root);

} // namespace iron3d
