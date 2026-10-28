#pragma once

// Gizmo de transformacion (Fase 2b, siguiente paso tras el Gestor de Objetos
// de la Fase 2): handles de mover/rotar/escalar sobre el objeto seleccionado,
// como en cualquier DCC. Diseno propio: ejes SIEMPRE en espacio de mundo
// (modo "global", el mas simple y robusto de implementar sin componer
// rotaciones locales acumuladas) -- limitacion conocida y asumida: si el
// PADRE del nodo seleccionado tiene su propia rotacion, arrastrar un eje no
// coincide exactamente con el eje local del objeto. Documentado aqui en vez
// de en el codigo disperso porque es la decision de diseno mas relevante de
// este archivo.
//
// Picking/arrastre: el rayo del raton se compara contra la recta de cada eje
// (mover/escalar) o contra el plano perpendicular a cada eje (rotar). El
// arrastre usa SIEMPRE la linea/plano ancladas en la posicion de inicio
// (originWorld_), nunca la posicion actual del nodo -- evita que el gizmo
// "se escape" bajo el cursor mientras se arrastra.

#include <glm/glm.hpp>
#include <vector>

#include "Vertex.h"

namespace iron3d {

class SceneNode;

enum class GizmoMode { Translate, Rotate, Scale };
enum class GizmoAxis { None, X, Y, Z };

struct Ray {
    glm::vec3 origin{0.0f};
    glm::vec3 direction{0.0f, 0.0f, 1.0f}; // normalizado
};

class TransformGizmo {
public:
    GizmoMode mode = GizmoMode::Translate;

    // Construye el rayo de mundo bajo el cursor (coordenadas de pantalla en
    // pixeles, origen arriba-izquierda -- las mismas que da GLFW).
    static Ray screenRay(double mouseX, double mouseY, int width, int height,
                          const glm::mat4& view, const glm::mat4& proj, const glm::vec3& eye);

    // Sin arrastrar: solo actualiza que eje queda resaltado bajo el cursor.
    void updateHover(const Ray& ray, const SceneNode* node, const glm::vec3& eye);

    // Intenta empezar un arrastre sobre `node`. Devuelve false (y no cambia
    // nada) si el rayo no cae sobre ningun eje del gizmo.
    bool beginDrag(const Ray& ray, SceneNode* node, const glm::vec3& eye);
    void updateDrag(const Ray& ray);
    void endDrag();
    bool isDragging() const { return dragging_; }

    // Geometria a dibujar este frame para `node` (nullptr -> listas vacias).
    // `eye` fija el tamano del gizmo en pantalla (mas lejos = mas grande en
    // mundo, para verse igual de grande en pantalla).
    void buildGeometry(const SceneNode* node, const glm::vec3& eye,
                        std::vector<Vertex>& outLines, std::vector<Vertex>& outTriangles) const;

private:
    GizmoAxis hovered_ = GizmoAxis::None;
    GizmoAxis active_ = GizmoAxis::None;
    bool dragging_ = false;
    SceneNode* target_ = nullptr;

    // Congelados al pulsar -- el arrastre completo se mide contra esto.
    glm::vec3 originWorld_{0.0f};
    glm::mat3 parentInverseRot_{1.0f};
    glm::vec3 startPosition_{0.0f};
    glm::vec3 startRotationDeg_{0.0f};
    glm::vec3 startScale_{1.0f};
    float startParam_ = 0.0f; // mover/escalar: distancia con signo a lo largo del eje
    float startAngle_ = 0.0f; // rotar: angulo inicial en el plano del eje

    // Comun a hover/pick: recalcula el eje bajo el rayo para el nodo (origen
    // de mundo RECIEN calculado, no el congelado). `outParam` recibe la
    // distancia a lo largo del eje (mover/escalar) o el angulo (rotar) en el
    // punto detectado -- solo valido si el resultado no es GizmoAxis::None.
    GizmoAxis pickAxis(const Ray& ray, const SceneNode* node, const glm::vec3& eye, float* outParam) const;
};

} // namespace iron3d
