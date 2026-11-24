#pragma once

// Deformadores y generadores de Iron3D que van mas alla de los de Deform.cpp (2026-11-24). Todos reciben la
// malla del primer hijo ya en el espacio del nodo (como deformMeshData) y devuelven la malla que dibuja el nodo.
//
// Tipos (DeformData::kind):
//   13 Jiggle          muelle que retrasa la parte alta del hijo cuando se mueve (pista de posicion del hijo)
//   14 FX Explosion    poligonos que salen despedidos, giran y caen con gravedad (amount = tiempo, 0..100 %)
//   15 Romper          poligonos que caen al suelo (plano inferior del objeto) con retardo aleatorio
//   16 Correccion      deja pasar la malla tal cual (para Hacer Editable y corregir puntos a mano)
//   17 Cubrir          lleva cada punto al punto mas cercano de la superficie del 2.o hijo
//   18 Superficie      proyecta cada punto, a lo largo del eje, sobre la superficie del 2.o hijo
//   19 Envoltura       enrolla el objeto en un cilindro alrededor del eje (100 % = vuelta completa)
//   20 Camara          rejilla 2D (4 esquinas de FFD) en el plano perpendicular al eje
//   21 Colision        saca fuera del 2.o hijo los puntos que quedan dentro de el
//   22 Desplazador     ruido 3D a lo largo de la normal (amount = altura en cm, param2 = tamano del ruido en cm)
//   23 Formula         desplazamiento a lo largo de la normal dado por una formula (x, y, z, u, v, t)
//   24 Viento          onda que recorre el objeto con el tiempo (amount = amplitud %, param2 = frecuencia)
//   25 Suavizado       relajacion laplaciana de los puntos (amount = fuerza %, param2 = iteraciones)
//   26 Malla           jaula: el 2.o hijo es la jaula; al mover sus puntos se mueve el objeto
//   27 Spline          (reservado para el deformador Spline; sin efecto hasta que haya splines)
//   28 Booleano        2 primeros hijos: param2 0 Union, 1 Sustraccion (A-B), 2 Interseccion
//   29 Conector        une todas las mallas hijas en una sola, soldando puntos a menos de amount cm
//   30 Reduccion       reduce los poligonos del hijo al amount % (agrupacion de puntos en rejilla)
//   31 Atomos          esferas en los puntos (param2 = radio cm) y cilindros en las aristas (amount = radio cm)
//   32 Metaball        superficie que envuelve a los hijos (esferas por su caja); param2 = resolucion en cm
//   33 LOD             muestra un hijo segun la distancia a la camara (amount = cm por nivel)
//   34 Matriz          copias del hijo en circulo alrededor del eje (param2 = copias, amount = radio cm)
//   35 Matriz MoGraph  rejilla de cubos (param2 = cubos por lado, amount = separacion cm)
//   36 Fractura        separa las piezas sueltas del hijo y las aleja del centro (amount = distancia cm)
//   37 Fractura Voronoi trozos por celdas de Voronoi (param2 = celdas), separados amount cm; sin tapas
//   38 MoInstance      estela: copias del hijo en fotogramas anteriores (param2 = copias, amount = paso en fotogramas)
//   39 MoExtrude       extruye todos los poligonos (amount = distancia cm, param2 = pasos)
//   40 PolyFX          cada poligono se separa y se desplaza/gira al azar (amount = intensidad %)

#include "MeshData.h"
#include "SceneNode.h"

#include <glm/glm.hpp>

#include <string>
#include <utility>
#include <vector>

namespace iron3d {

constexpr int kDeformKindCount = 41;
// Nombre del tipo (clave de traduccion, en espanol).
const char* deformKindName(int kind);
// true si el resultado depende del fotograma (se recalcula al cambiarlo).
bool deformKindUsesTime(int kind);
// true si el tipo usa todos los hijos (Conector, Metaball, LOD) en vez del primero.
bool deformKindUsesAllChildren(int kind);
// true si el tipo no necesita hijo con malla (Matriz MoGraph).
bool deformKindNeedsNoChild(int kind);
// Etiquetas de amount y param2 para el panel (claves de traduccion; nullptr = no se usa).
const char* deformAmountLabel(int kind);
const char* deformParam2Label(int kind);
float deformDefaultAmount(int kind);
float deformDefaultParam2(int kind);

struct DeformMoreInput {
    float frame = 0.0f;
    float fps = 30.0f;
    float param2 = 1.0f;
    const AnimTrack* motion = nullptr;             // pista de posicion del primer hijo (Jiggle, MoInstance)
    glm::mat4 parentToNode{1.0f};                  // MoInstance: matriz del padre del hijo (identidad = el nodo)
    const MeshData* second = nullptr;              // 2.o hijo
    glm::mat4 secondMatrix{1.0f};
    const glm::vec3* ffd = nullptr;                // Camara: 4 esquinas
    const std::string* formula = nullptr;          // Formula
    const MeshData* rest = nullptr;                // Malla: jaula en reposo
    std::vector<std::pair<const MeshData*, glm::mat4>> all;  // Conector / Metaball / LOD: hijos con malla y su matriz
    std::vector<std::pair<glm::vec3, float>> spheres;        // Metaball: centro y radio de cada hijo (espacio del nodo)
    glm::vec3 viewPointNode{0.0f, 0.0f, 1e9f};     // LOD: camara en el espacio del nodo
};

// `src` ya esta en el espacio del nodo. axis 0..2.
MeshData deformMore(MeshData src, int kind, int axis, float amount, const DeformMoreInput& in);

// Formula del deformador Formula: devuelve false si no se entiende (y deja `err`).
bool evalFormula(const std::string& expr, float x, float y, float z, float u, float v, float t, float& out,
                 std::string* err = nullptr);

// Punto de vista para el LOD (en mundo); lo fija la interfaz cada fotograma.
void setLodViewPoint(const glm::vec3& world);
const glm::vec3& lodViewPoint();

} // namespace iron3d
