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
// `Camera` (2026-11-07) = Camara de C4D, con sus datos en SceneNode::camera.
enum class PrimitiveType { None, Cube, Sphere, Plane, Light, Mesh, Camera };
const char* primitiveTypeName(PrimitiveType type); // nombre para la UI, en espanol

// Rotacion HPB de C4D (2026-11-06): R = Ry(H) * Rx(P) * Rz(B), es decir,
// primero Banco (Z), luego Pitch (X) y por ultimo Heading (Y). `deg` va en
// SceneNode::rotationDeg: x = P, y = H, z = B. Todo el que componga o
// descomponga rotaciones (nodo, gizmo, importador/exportador .c4d) usa estas
// dos funciones, asi los numeros H/P/B salen como en C4D.
glm::mat3 hpbToMat3(const glm::vec3& deg);
glm::vec3 mat3ToHpb(const glm::mat3& rotation); // `rotation` sin escala
// La convencion anterior (escenas .iron de version < 4): R = Rz * Ry * Rx.
glm::mat3 legacyXyzToMat3(const glm::vec3& deg);

// Estado de visibilidad de C4D: Por Defecto (gris, hereda del padre),
// Activado (verde, visible aunque el padre este oculto) o Apagado (rojo).
enum class VisMode { Default = 0, On = 1, Off = 2 };
inline bool resolveVisibility(VisMode mode, bool parentVisible) {
    return mode == VisMode::On ? true : (mode == VisMode::Off ? false : parentVisible);
}

// Animacion por fotogramas clave (F9), como las pistas de C4D: una pista por
// canal (Posicion, Rotacion H/P/B, Escala), claves ordenadas por fotograma e
// interpolacion spline con tangentes automaticas recortadas (sin rebasar los
// valores de las claves, como el "Clamp" de C4D). Antes de la primera clave y
// despues de la ultima el valor se queda fijo.
struct AnimKey {
    int frame = 0;
    glm::vec3 value{0.0f};
};
struct AnimTrack {
    std::vector<AnimKey> keys;
    bool empty() const { return keys.empty(); }
    void set(int frame, const glm::vec3& value);  // anade o sustituye
    bool remove(int frame);
    bool hasKeyAt(int frame) const;
    glm::vec3 evaluate(float frame) const;         // requiere !empty()
};

// Datos de una Luz (2026-11-07), con los mismos valores que la Luz de C4D
// para que un .c4d vaya y vuelva igual. Las distancias en unidades de Iron3D.
enum class LightKind {
    Omni = 0, Spot = 1, SpotSquare = 2, Infinite = 3, Parallel = 4, ParallelSpot = 5, ParallelSpotSquare = 6,
    Tube = 7, Area = 8, Photometric = 9
};
const char* lightKindName(LightKind kind); // en espanol
struct LightData {
    LightKind kind = LightKind::Omni;
    glm::vec3 color{1.0f};
    float intensity = 1.0f;          // 1 = 100 %
    float outerAngleDeg = 30.0f;     // Foco: angulo exterior (C4D: 30)
    float innerAngleDeg = 0.0f;      // Foco: angulo interior
    int shadow = 0;                  // 0 ninguna, 1 suave, 2 trazada (dura), 3 de area
    int falloff = 0;                 // LIGHT_DETAILS_FALLOFF de C4D (0 ninguno, 10 cuadrado inverso...)
    float falloffRadius = 2.5f;      // Radio de decaimiento (C4D: 500 cm)
    float innerRadius = 0.0f;        // Radio interior del decaimiento
    int areaShape = 1;               // forma de la Luz de Area (1 = rectangulo)
    glm::vec3 areaSize{1.0f, 1.0f, 1.0f}; // tamano X/Y/Z de la Luz de Area (C4D: 200 cm)
    bool operator==(const LightData&) const = default;
};

// Datos de una Camara (2026-11-07), como la Camara de C4D.
struct CameraData {
    int projection = 0;              // CAMERA_PROJECTION de C4D (0 perspectiva, 1 paralela...)
    float focalMm = 36.0f;           // Distancia focal
    float sensorMm = 36.0f;          // Tamano del sensor (Apertura horizontal)
    float zoom = 1.0f;               // Zoom (proyecciones paralelas)
    // Etiqueta Objetivo: ruta por nombres desde la raiz ("Grupo/Cubo"); vacia = sin objetivo.
    std::string target;
    float fovDeg() const;            // campo de vision horizontal que sale de focal y sensor
    bool operator==(const CameraData&) const = default;
};

// Etiqueta de Textura del nodo (la que asigna materialIndex): proyeccion y
// mosaico como en C4D (2026-11-07).
struct TextureTagData {
    int projection = 6;              // TEXTURETAG_PROJECTION de C4D (6 = UVW)
    glm::vec2 offset{0.0f};          // Desplazamiento U/V (0..1)
    glm::vec2 length{1.0f};          // Longitud U/V (mosaico = 1 / longitud)
    bool tile = true;
    bool seamless = false;
    int side = 0;                    // Lado: 0 ambos, 1 frente, 2 detras
    bool operator==(const TextureTagData&) const = default;
};

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
    // Reparentado (Agrupar / Expandir Grupo / Eliminar Sin Hijos del Gestor de
    // Objetos): saca `child` del arbol sin destruirlo, o lo inserta en la
    // posicion `index` (se recorta al final si se pasa).
    std::unique_ptr<SceneNode> detachChild(SceneNode* child);
    SceneNode* insertChild(std::unique_ptr<SceneNode> child, size_t index);
    // Posicion de `child` entre los hijos, o children().size() si no es hijo.
    size_t indexOf(const SceneNode* child) const;
    // Inverso de localMatrix(): posicion, rotacion X/Y/Z y escala a partir de
    // una matriz T*Rz*Ry*Rx*S (sin cizalla). Sirve para conservar la posicion
    // en mundo al cambiar de padre.
    void setLocalMatrix(const glm::mat4& m);

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

    // Solo con primitive == Light / Camera; texTag acompana a materialIndex.
    LightData light;
    CameraData camera;
    TextureTagData texTag;

    // Pistas de animacion (vacias = sin animar).
    AnimTrack animPos, animRot, animScale;
    bool hasAnimation() const { return !animPos.empty() || !animRot.empty() || !animScale.empty(); }

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

// Pone posicion/rotacion/escala de todo el arbol en el fotograma `frame`
// segun sus pistas. Los nodos sin pistas no se tocan.
void evaluateAnimation(SceneNode& root, float frame);
// Graba una clave en el fotograma `frame` con los valores actuales del nodo,
// solo en los canales pedidos (como los botones de Grabar Posicion/Escala/
// Rotacion de C4D).
void recordKey(SceneNode& node, int frame, bool position, bool rotation, bool scale);
// Fotogramas con alguna clave en el nodo, ordenados y sin repetir.
std::vector<int> keyFrames(const SceneNode& node);

} // namespace iron3d
