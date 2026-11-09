#pragma once

// Gizmo de transformacion (Fase 2b, siguiente paso tras el Gestor de Objetos
// de la Fase 2): handles de mover/rotar/escalar sobre el objeto seleccionado,
// como en cualquier DCC. Ejes del OBJETO por defecto, como C4D; con
// `worldAxes` (tecla W) los del mundo. Las rotaciones se componen como
// matrices en el espacio del padre y se vuelven a pasar a H/P/B eligiendo
// los angulos mas cercanos a los de antes, para que el valor no salte de
// 180 a -180 a mitad de arrastre.
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
// `View` solo aplica en modo Rotar, y reproduce el comportamiento documentado
// del Rotar de Cinema 4D (help.maxon.net/c4d/en-us/Content/html/
// TOOLROTATE.html, leido 2026-10-30 -- cita textual, no decompilado):
// "Rotation can take place within or outside of the rotation circle. It is
// displayed as a circle that is always parallel to the view plane. Drag
// from an empty part within the circle to rotate in any direction or from
// an empty part outside the circle to rotate on the same plane as the
// circle." `View` = fuera del circulo (plano de vista, eje camara-objeto).
// `Free` dentro del circulo en Rotar es el trackball de arriba. En Mover y
// Escalar (decision de Core 2026-10-30, no viene de ningun doc de C4D: "los
// modos libres... cuando le das a un espacio libre y mantienes click
// izquierdo") `Free` es el equivalente -- clic fuera de las 3 flechas/
// handles de eje, en vez de caer a seleccion/deseleccion. Mover-Free
// desplaza en el plano de vista (mismo eje camara-objeto que el anillo View
// de Rotar); Escalar-Free escala uniformemente los 3 ejes segun la
// distancia en pantalla al origen del gizmo. Ver updateDrag().
enum class GizmoAxis { None, X, Y, Z, View, Free };

struct Ray {
    glm::vec3 origin{0.0f};
    glm::vec3 direction{0.0f, 0.0f, 1.0f}; // normalizado
};

class TransformGizmo {
public:
    GizmoMode mode = GizmoMode::Translate;
    // "Usar Sistema de Coordenadas de Mundo" (W de C4D): false = ejes del
    // objeto (por defecto en C4D), true = ejes del mundo.
    bool worldAxes = false;

    // Con varios objetos seleccionados, C4D pone el gizmo en el centro de la
    // seleccion y no sobre un objeto concreto. `usePivot` cambia el origen de
    // dibujo y de picking a `pivotWorld`; el objeto que se transforma sigue
    // siendo el activo (el resto lo reparte la UI, ver applySelectionDrag).
    bool usePivot = false;
    glm::vec3 pivotWorld{0.0f};

    glm::vec3 originFor(const SceneNode* node) const;

    // Construye el rayo de mundo bajo el cursor (coordenadas de pantalla en
    // pixeles, origen arriba-izquierda -- las mismas que da GLFW).
    static Ray screenRay(double mouseX, double mouseY, int width, int height,
                          const glm::mat4& view, const glm::mat4& proj, const glm::vec3& eye);

    // Sin arrastrar: solo actualiza que eje queda resaltado bajo el cursor.
    // mouseX/mouseY = cursor en pixeles LOCALES al viewport (igual que
    // screenRay); viewportW/H = tamano del viewport en pixeles; viewProj =
    // proj*view -- necesarios para el picking de los aros de Rotar en
    // espacio de pantalla (ver pickAxis).
    void updateHover(const Ray& ray, const SceneNode* node, const glm::vec3& eye,
                      double mouseX, double mouseY, int viewportW, int viewportH,
                      const glm::mat4& viewProj);

    // Intenta empezar un arrastre sobre `node`. Devuelve false (y no cambia
    // nada) si el rayo no cae sobre ningun eje del gizmo. camRight/camUp =
    // base de camara en espacio de mundo (filas de la matriz view), SOLO
    // para congelar la rotacion "Free"/trackball si el arrastre acaba
    // siendo de ese tipo -- el resto de modos no la usan.
    // `force` != None: arranca ese modo sin mirar donde cae el clic (main.cpp
    // lo usa para el arrastre Free que sigue a un clic de seleccion).
    bool beginDrag(const Ray& ray, SceneNode* node, const glm::vec3& eye,
                    double mouseX, double mouseY, int viewportW, int viewportH,
                    const glm::mat4& viewProj, const glm::vec3& camRight, const glm::vec3& camUp,
                    GizmoAxis force = GizmoAxis::None);

    // Handle de verdad bajo el cursor: flecha X/Y/Z, aro X/Y/Z o, en Rotar,
    // el aro exterior de vista. None si el clic cae fuera de todos (aunque
    // beginDrag lo tomaria como Free/View): ahi main.cpp prueba antes a
    // seleccionar, como C4D.
    GizmoAxis pickHandle(const Ray& ray, const SceneNode* node, const glm::vec3& eye,
                          double mouseX, double mouseY, int viewportW, int viewportH,
                          const glm::mat4& viewProj) const;
    // mouseX/mouseY: solo los usa el modo Free (trackball) para medir el
    // arrastre en pixeles de pantalla; el resto de modos siguen usando
    // unicamente `ray`.
    void updateDrag(const Ray& ray, double mouseX, double mouseY);
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
    glm::mat3 parentRot_{1.0f};      // rotacion pura del padre (columnas normalizadas)
    glm::vec3 axes_[3];              // ejes del gizmo congelados al pulsar

    void axesFor(const SceneNode* node, glm::vec3 out[3]) const;
    // Aplica una rotacion de MUNDO `d` sobre la orientacion de partida.
    void applyWorldRotation(const glm::mat3& d);
    glm::vec3 startPosition_{0.0f};
    glm::vec3 startRotationDeg_{0.0f};
    glm::vec3 startScale_{1.0f};
    float startParam_ = 0.0f; // mover/escalar: distancia con signo a lo largo del eje
    float startAngle_ = 0.0f; // rotar: angulo inicial en el plano del eje

    // Anillo "View" (rotar libre): eje camara-objeto y su base angular,
    // CONGELADOS en beginDrag -- updateDrag no recibe la camara, asi que no
    // hay forma de recalcularlos a mitad de arrastre aunque se quisiera, lo
    // cual es intencional: evita que una camara que se mueva un pixel entre
    // beginDrag y updateDrag meta un salto de angulo espurio.
    glm::vec3 viewAxisWorld_{0.0f, 0.0f, 1.0f};
    glm::vec3 viewBasisU_{1.0f, 0.0f, 0.0f};
    glm::vec3 viewBasisV_{0.0f, 1.0f, 0.0f};

    // Rotacion "Free" (trackball, dentro del circulo): arcball de Shoemake
    // (Ken Shoemake, "ARCBALL: A User Interface for Specifying
    // Three-Dimensional Orientation Using a Mouse", Graphics Interface '92
    // -- algoritmo publico de toda la vida, nunca codigo de Maxon). Cursor
    // de pantalla -> punto sobre una semiesfera virtual del MISMO radio en
    // pantalla que el circulo View ya dibujado (freeScreenRadius_), para
    // que la sensacion de arrastre coincida con el tamano visual real del
    // gizmo en vez de depender de una constante de sensibilidad inventada.
    // Todo congelado en beginDrag -- el delta se mide siempre contra el
    // punto de inicio del arrastre, nunca contra el frame anterior.
    glm::vec3 freeCamRight_{1.0f, 0.0f, 0.0f};
    glm::vec3 freeCamUp_{0.0f, 1.0f, 0.0f};
    // freeScreenCenter_ tambien la reutilizan Mover/Escalar en modo Free (el
    // origen del gizmo proyectado a pantalla, congelado al pulsar) -- mismo
    // campo, incluso aunque solo uno de los tres contextos este activo en un
    // arrastre dado.
    glm::vec2 freeScreenCenter_{0.0f, 0.0f};
    float freeScreenRadius_ = 1.0f;
    glm::vec3 freeStartPoint_{0.0f, 0.0f, 1.0f}; // Rotar-Free: punto de inicio sobre la semiesfera
    glm::vec3 freeDragStartWorld_{0.0f}; // Mover-Free: punto de inicio sobre el plano de vista

    // Comun a hover/pick: recalcula el eje bajo el rayo para el nodo (origen
    // de mundo RECIEN calculado, no el congelado). `outParam` recibe la
    // distancia a lo largo del eje (mover/escalar) o el angulo (rotar) en el
    // punto detectado -- solo valido si el resultado no es GizmoAxis::None.
    // mouseX/mouseY/viewportW/H/viewProj: solo los usa el modo Rotar, para
    // medir distancia al aro en pixeles de pantalla en vez de en el plano 3D
    // del aro (ver nota en el .cpp sobre vision rasante).
    GizmoAxis pickAxis(const Ray& ray, const SceneNode* node, const glm::vec3& eye,
                        double mouseX, double mouseY, int viewportW, int viewportH,
                        const glm::mat4& viewProj, float* outParam) const;
};

} // namespace iron3d
