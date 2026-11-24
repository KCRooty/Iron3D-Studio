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
//
// Interpolacion por clave (2026-11-07): manda en el tramo que EMPIEZA en esa
// clave, como en C4D -- Spline (tangentes automaticas), Lineal o Escalon (el
// valor se queda hasta la clave siguiente).
enum class KeyInterp { Spline, Linear, Step };
struct AnimKey {
    int frame = 0;
    glm::vec3 value{0.0f};
    KeyInterp interp = KeyInterp::Spline;
};
struct AnimTrack {
    std::vector<AnimKey> keys;
    bool empty() const { return keys.empty(); }
    void set(int frame, const glm::vec3& value);  // anade o sustituye (conserva la interpolacion)
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
    // Pestana General de C4D (2026-11-10): No Ilumina, Difusion, Especular.
    bool noIllumination = false;
    bool diffuse = true;
    bool specular = true;
    // Etiqueta Objetivo: ruta por nombres desde la raiz; la luz se orienta hacia ese objeto (vacia = sin objetivo).
    std::string target;
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

// Etiqueta IK de C4D (ID_CA_IK_TAG): cadena que acaba en `tip` (vacio = el propio
// objeto) y apunta a `target`; `pole` es el vector polo. Las referencias son rutas
// por nombres desde la raiz, como CameraData::target. Por ahora solo se conserva
// (importar/guardar/duplicar); resolverla es el solver IK pendiente.
struct IkTagData {
    bool present = false;
    bool enabled = true;
    int solver = 0;                  // ID_CA_IK_TAG_SOLVER: 0 = 2D, 1 = 3D
    std::string tip, target, pole;
    bool operator==(const IkTagData&) const = default;
};

// Objeto Simetria (generador de Modelado, ver Symmetry.h). La cache evita recalcular el
// reflejo mientras ni el hijo ni los ajustes cambian; no se guarda ni se copia.
struct SymmetryData {
    bool present = false;
    int axis = 0;          // plano reflejado: 0 = X, 1 = Y, 2 = Z del nodo
    float tolCm = 0.5f;    // los puntos a menos de esto del plano se pegan a el
    std::shared_ptr<const MeshData> output, lastSource;
    glm::mat4 lastChildMatrix{1.0f};
    int lastAxis = -1;
    float lastTolCm = -1.0f;
};

// Spline (ver Spline.h): curva de C4D. kind 0 = puntos libres; el resto son primitivas con parametros (cm).
// `extra` = contornos adicionales (texto o booleanas hechos editables, salida de un generador).
struct SplineData {
    bool present = false;
    int kind = 0;
    int plane = 0;             // 0 XY, 1 ZY, 2 XZ
    int interp = 1;            // 0 lineal, 1 suave
    bool closed = false;
    std::vector<glm::vec3> points;
    std::vector<std::vector<glm::vec3>> extra;
    std::vector<char> extraClosed;
    float a = 200.0f, b = 100.0f, c = 0.0f;
    int n = 8;
    float startDeg = 0.0f, endDeg = 90.0f;
    std::string text = "Texto";  // Texto; ruta de la imagen en Vectorizador
    std::string fx = "200*cos(t*2*pi)", fy = "200*sin(t*2*pi)", fz = "0";
    bool generated = false;      // salida de un generador: no se guarda ni se edita
    bool operator==(const SplineData&) const = default;
};

// Deformador (generador de Deformadores, ver Deform.h): Doblar, Enroscar, Afilar, Hinchar, Sesgar, Aplastar y Estirar,
// Esferizar, Derretir o Subdivision Superficie (kind 8, amount = subdivisiones).
struct DeformData {
    bool present = false;
    int kind = 0;       // 0 Doblar, 1 Enroscar, 2 Afilar, 3 Hinchar, 4 Sesgar, 5 Aplastar, 6 Esferizar, 7 Derretir, 8 Subdivision,
                        // 9 Explosion, 10 Morph (2.o hijo = destino), 11 FFD (rejilla 2x2x2), 12 Bisel; 13..40 en DeformMore.h
    int axis = 1;       // eje del nodo a lo largo del cual actua (0 X, 1 Y, 2 Z)
    float amount = 45;  // grados (Doblar, Enroscar) o % (Afilar, Hinchar, Sesgar)
    std::shared_ptr<const MeshData> output, lastSource;
    glm::mat4 lastChildMatrix{1.0f};
    int lastKind = -1, lastAxis = -1;
    float lastAmount = 1e9f;
    // FFD: desplazamiento de las 8 esquinas de la caja, como fraccion de su tamano (indice = x + 2y + 4z).
    glm::vec3 ffd[8] = {};
    // Morph: cache del segundo hijo y de la rejilla.
    std::shared_ptr<const MeshData> lastSource2;
    glm::mat4 lastChildMatrix2{1.0f};
    glm::vec3 lastFfd[8] = {};
    // Tipos de DeformMore.h (2026-11-24): segundo parametro, formula y jaula en reposo (deformador Malla).
    float param2 = 1.0f;
    std::string formula = "sin((u+t)*2*pi)*0.2";
    std::shared_ptr<const MeshData> bindRest;
    float lastParam2 = 1e9f, lastFrame = -1e9f;
    std::string lastFormula;
    std::vector<std::shared_ptr<const MeshData>> lastAllMeshes;
    std::vector<glm::mat4> lastAllMats;
    glm::vec3 lastView{1e9f};
    std::vector<SplineData> lastSplines;
    std::vector<glm::mat4> lastSplineMats;
};

// Objeto Instancia (2026-11-20): comparte la malla de otro objeto (ruta por nombres desde la raiz, como
// CameraData::target) y se ve igual que el original aunque se edite despues. Tiene su propia transformacion.
struct InstanceData {
    bool present = false;
    std::string source;
};

// Objetos de escena Fondo y Cielo (2026-11-20): colocan el color de fondo del render en el Visor de Imagenes
// (degradado de `top` a `bottom`). kind: 0 Fondo, 1 Cielo, 2 Entorno (color y nivel de la luz ambiente del visor), 3 Primer Plano (degradado `top`-`bottom` con opacidad `level` sobre el render).
struct EnvData {
    bool present = false;
    int kind = 0;
    glm::vec3 top{0.2f, 0.2f, 0.25f};
    glm::vec3 bottom{0.2f, 0.2f, 0.25f};
    float level = 1.0f; // Entorno: multiplica el color ambiente
};

// Clonador (ver Clone.h): copias del primer hijo con malla en modo Lineal, Cuadricula o Radial.
struct CloneData {
    bool present = false;
    int mode = 0;               // 0 Lineal, 1 Cuadricula, 2 Radial, 3 Objeto (sobre los puntos del 2.o hijo con malla)
    int count = 3;              // copias (Lineal, Radial) o en X (Cuadricula)
    int countY = 1, countZ = 1; // solo Cuadricula
    glm::vec3 offset{100.0f, 0.0f, 0.0f};  // cm: paso (Lineal) o separacion en X/Y/Z (Cuadricula)
    float radiusCm = 200.0f;    // solo Radial
    int axis = 1;               // Radial: eje normal al circulo (0 X, 1 Y, 2 Z)
    float scaleStepPct = 0.0f;  // % de escala que se suma por copia
    float rotStepDeg = 0.0f;    // grados que se suman por copia
    // Efector Aleatorio: cada copia se desplaza/gira/escala un valor al azar (reproducible con `seed`).
    glm::vec3 randPosCm{0.0f};  // desplazamiento maximo por eje, en cm
    float randRotDeg = 0.0f;    // giro maximo extra, en grados
    float randScalePct = 0.0f;  // variacion maxima de escala, en %
    int seed = 1;
    std::shared_ptr<const MeshData> output, lastSource;
    glm::mat4 lastChildMatrix{1.0f};
    int lastMode = -1, lastCount = -1, lastCountY = -1, lastCountZ = -1, lastAxis = -1;
    glm::vec3 lastOffset{1e9f};
    float lastRadius = -1.0f, lastScaleStep = 1e9f, lastRotStep = 1e9f;
    glm::vec3 lastRandPos{1e9f};
    float lastRandRot = 1e9f, lastRandScale = 1e9f;
    int lastSeed = -1;
    std::shared_ptr<const MeshData> lastSource2;  // modo Objeto: malla que da los puntos
    glm::mat4 lastMatrix2{1.0f};
};

// Limites de rotacion de una articulacion (Ojoint, pestana IK > Limites de C4D, ids 3009..3015
// de ocajoint.h). Orden H, P, B en grados, como en C4D. Solo se usan los ejes con use = true.
struct JointLimits {
    bool present = false;
    bool use[3] = {false, false, false};
    glm::vec3 minDeg{0.0f}, maxDeg{0.0f};
    float strength = 1.0f; // ID_CA_JOINT_OBJECT_JOINT_IK_LIMITROT_STRENGTH (0..1)
    bool operator==(const JointLimits&) const = default;
};

// Etiqueta Proteccion de C4D (Tprotection, tprotection.h): por eje de posicion, escala y rotacion, deja
// el valor libre, lo bloquea o lo limita entre un minimo y un maximo. En los rigs es la etiqueta
// "Limite" de los controladores. Los datos ya estan en los ejes de Iron3D (x, y, z de position, scale
// y rotationDeg), en unidades de escena y grados.
struct ProtectionData {
    enum Mode { Free = 0, Lock = 1, Limit = 2 };
    bool present = false;
    int posMode[3] = {0, 0, 0}, sclMode[3] = {0, 0, 0}, rotMode[3] = {0, 0, 0};
    glm::vec3 posMin{0.0f}, posMax{0.0f}, sclMin{0.0f}, sclMax{0.0f}, rotMin{0.0f}, rotMax{0.0f};
    bool allowExpressions = true; // PROTECTION_ALLOW_EXPRESSIONS: la IK y la animacion no se limitan
    bool operator==(const ProtectionData&) const = default;
};

// Forma de visualizacion de un Nulo (onull.h: NULLOBJECT_DISPLAY/RADIUS/ASPECTRATIO/ORIENTATION).
// shape < 0 = sin forma (no se dibuja); si no, un NullShape de core/NullShapes.h. Radio en unidades de escena.
struct NullDisplay {
    int shape = -1;
    float radius = 0.25f;   // 50 cm, el valor por defecto de C4D
    float aspect = 1.0f;
    int orientation = 0;    // 0 Pantalla (se dibuja como XY), 1 XY, 2 ZY, 3 XZ
    bool operator==(const NullDisplay&) const = default;
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

// Parte de una malla con otro material (2026-11-11): en C4D, una etiqueta de
// Textura restringida a una Seleccion de Poligonos. Rango de mesh->indices;
// los triangulos que no cubre ninguna parte usan materialIndex/texTag.
struct MaterialPart {
    uint32_t firstIndex = 0;
    uint32_t indexCount = 0;
    int materialIndex = -1;
    TextureTagData texTag;
    std::string selection;           // nombre de la Seleccion de Poligonos en C4D
    bool operator==(const MaterialPart&) const = default;
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
    // Mover nodos a otro padre (Gestor de Objetos: arrastrar y soltar). `index` es la posicion en
    // newParent antes de sacar nada; los nodos conservan su posicion en el mundo y su orden relativo.
    // canReparentNodes: false si hay un nodo sin padre (la raiz) o si newParent es uno de ellos o cuelga de
    // uno (un objeto no puede ir dentro de si mismo). reparentNodes no toca nada cuando es false.
    static bool canReparentNodes(const std::vector<SceneNode*>& nodes, const SceneNode& newParent);
    static bool reparentNodes(const std::vector<SceneNode*>& nodes, SceneNode& newParent, size_t index);
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
    // Poligonos ocultos del editor (Seleccion > Ocultar). Solo vale mientras
    // la malla sea la misma (hiddenFor): cualquier edicion crea otra malla y
    // los poligonos vuelven a verse.
    std::shared_ptr<const std::vector<char>> hiddenPolys;
    const MeshData* hiddenFor = nullptr;
    bool hasHidden() const { return hiddenPolys && mesh && hiddenFor == mesh.get(); }

    // Solo con primitive == Light / Camera; texTag acompana a materialIndex.
    LightData light;
    CameraData camera;
    TextureTagData texTag;
    // Rig: articulacion de C4D (se muestra como Nulo) y etiqueta IK.
    bool isJoint = false;
    JointLimits jointLimits;
    ProtectionData protection; // etiqueta Proteccion/Limite
    NullDisplay nullDisplay;
    IkTagData ik;
    SymmetryData symmetry;
    DeformData deform;
    InstanceData instance;
    EnvData env;
    SplineData spline;
    CloneData clone;
    ParamShape param;  // primitiva poligonal con parametros editables (ver MeshData.h)
    // Solo con primitive == Mesh: partes con su propio material, ordenadas por
    // firstIndex y sin solaparse (ver MaterialPart).
    std::vector<MaterialPart> materialParts;
    // Piel en vivo (ver Skin.h); compartida entre copias hasta que alguien la edite.
    std::shared_ptr<struct SkinData> skin;

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

// Edicion de claves de la Linea de Tiempo / Editor de Curvas, sobre las tres
// pistas a la vez (las claves de esos fotogramas, en cualquier canal).
// moveKeys: desplaza `delta` fotogramas; una clave que cae sobre otra que no
// se mueve la sustituye. Devuelven cuantas claves se tocaron.
int moveKeys(SceneNode& node, const std::vector<int>& frames, int delta);
int deleteKeys(SceneNode& node, const std::vector<int>& frames);
int setKeyInterp(SceneNode& node, const std::vector<int>& frames, KeyInterp interp);

// Rutas por nombres ("Grupo/Hueso~#2"). Con hermanos del mismo nombre (muy comun en rigs de C4D) el 2.o,
// 3.o... llevan el sufijo "~#2", "~#3", para que la ruta apunte siempre a un unico nodo.
// Segmento de ruta para el N-esimo (desde 1) hermano llamado `name`.
std::string pathSegmentFor(const std::string& name, int occurrence);
// Segmento de ruta de un nodo (su nombre, con "~#N" si es el N-esimo hermano con ese nombre).
std::string pathSegmentOf(const SceneNode& node);
// Hijo de `parent` que corresponde a un segmento (con o sin sufijo), o nullptr.
SceneNode* childByPathSegment(const SceneNode& parent, const std::string& segment);

} // namespace iron3d
