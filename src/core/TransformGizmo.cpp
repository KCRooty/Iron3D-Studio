#include "TransformGizmo.h"
#include "SceneNode.h"

#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/constants.hpp>
#include <cmath>
#include <algorithm>

namespace iron3d {

namespace {

constexpr float kScreenSizeFactor = 0.18f; // tamano del gizmo relativo a la distancia a la camara
constexpr int   kRingSegments = 48;
constexpr int   kConeSegments = 14;

const glm::vec3 kAxisDir[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
const glm::vec3 kAxisColor[3] = {{0.85f, 0.2f, 0.2f}, {0.25f, 0.8f, 0.3f}, {0.25f, 0.45f, 0.95f}};
const glm::vec3 kHighlightColor{0.95f, 0.85f, 0.15f};
const glm::vec3 kViewRingColor{0.7f, 0.7f, 0.75f}; // gris, como el anillo libre de C4D

int axisIndex(GizmoAxis a) { return a == GizmoAxis::X ? 0 : a == GizmoAxis::Y ? 1 : 2; }
GizmoAxis axisFromIndex(int i) { return i == 0 ? GizmoAxis::X : i == 1 ? GizmoAxis::Y : GizmoAxis::Z; }

float gizmoWorldSize(const glm::vec3& originWorld, const glm::vec3& eye) {
    return glm::distance(originWorld, eye) * kScreenSizeFactor;
}

// Vectores u,v ortonormales perpendiculares a `axis` -- base fija y
// consistente para medir angulos de rotacion y para dibujar el aro.
void basisForAxis(const glm::vec3& axis, glm::vec3& u, glm::vec3& v) {
    glm::vec3 helper = (std::abs(axis.y) > 0.99f) ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);
    u = glm::normalize(glm::cross(helper, axis));
    v = glm::cross(axis, u);
}

// Parametro a lo largo de la linea (lineOrigin + t*lineDir) mas cercano al
// rayo -- formula estandar de distancia entre dos rectas 3D (Ericson,
// "Real-Time Collision Detection" 5.1.9). Solo usamos `t` (la DIFERENCIA
// entre dos lecturas durante el arrastre), asi que pequenos errores de
// precision cerca de rectas casi paralelas no importan en la practica.
float closestLineParam(const Ray& ray, const glm::vec3& lineOrigin, const glm::vec3& lineDir) {
    const glm::vec3 r = ray.origin - lineOrigin;
    const float a = glm::dot(ray.direction, ray.direction);
    const float b = glm::dot(ray.direction, lineDir);
    const float e = glm::dot(lineDir, lineDir);
    const float c = glm::dot(ray.direction, r);
    const float f = glm::dot(lineDir, r);
    const float denom = a * e - b * b;
    if (std::abs(denom) > 1e-6f) {
        return (a * f - b * c) / denom;
    }
    return f / std::max(e, 1e-6f); // casi paralelas: proyeccion simple
}

// Interseccion rayo/plano (plano = `planePoint` + normal `n`). Devuelve
// false si el rayo es paralelo al plano o la interseccion queda detras.
bool rayPlaneIntersect(const Ray& ray, const glm::vec3& planePoint, const glm::vec3& n, glm::vec3& outPoint) {
    const float denom = glm::dot(n, ray.direction);
    if (std::abs(denom) < 1e-6f) return false;
    const float t = glm::dot(n, planePoint - ray.origin) / denom;
    if (t < 0.0f) return false;
    outPoint = ray.origin + ray.direction * t;
    return true;
}

void pushLine(std::vector<Vertex>& out, const glm::vec3& a, const glm::vec3& b, const glm::vec3& color) {
    out.push_back({a, color});
    out.push_back({b, color});
}

// Cono solido (punta de flecha de mover) con base circular de `segments`
// lados, apice en `tip`, centro de base en `base`, radio `radius`.
void pushCone(std::vector<Vertex>& out, const glm::vec3& base, const glm::vec3& tip,
              float radius, const glm::vec3& color, int segments) {
    glm::vec3 axis = glm::normalize(tip - base);
    glm::vec3 u, v;
    basisForAxis(axis, u, v);
    for (int i = 0; i < segments; ++i) {
        float a0 = (float)i / (float)segments * glm::two_pi<float>();
        float a1 = (float)(i + 1) / (float)segments * glm::two_pi<float>();
        glm::vec3 p0 = base + (u * std::cos(a0) + v * std::sin(a0)) * radius;
        glm::vec3 p1 = base + (u * std::cos(a1) + v * std::sin(a1)) * radius;
        out.push_back({tip, color});
        out.push_back({p0, color});
        out.push_back({p1, color});
    }
}

// Cubo solido pequeno (handle de escalar), centrado en `center`.
void pushCube(std::vector<Vertex>& out, const glm::vec3& center, float halfExtent, const glm::vec3& color) {
    const glm::vec3 h(halfExtent);
    const glm::vec3 c[8] = {
        center + glm::vec3(-h.x, -h.y, -h.z), center + glm::vec3(h.x, -h.y, -h.z),
        center + glm::vec3(h.x, h.y, -h.z),   center + glm::vec3(-h.x, h.y, -h.z),
        center + glm::vec3(-h.x, -h.y, h.z),  center + glm::vec3(h.x, -h.y, h.z),
        center + glm::vec3(h.x, h.y, h.z),    center + glm::vec3(-h.x, h.y, h.z),
    };
    const int idx[36] = {
        0, 1, 2, 2, 3, 0,  5, 4, 7, 7, 6, 5,  4, 0, 3, 3, 7, 4,
        1, 5, 6, 6, 2, 1,  3, 2, 6, 6, 7, 3,  4, 5, 1, 1, 0, 4,
    };
    for (int i : idx) out.push_back({c[i], color});
}

// Angulos (grados) <-> matriz con la MISMA convencion que
// SceneNode::localMatrix() (HPB de C4D, hpbToMat3) -- tiene que ser identica
// o la rotacion "salta" al pasar por el anillo View.
glm::mat3 eulerDegToMat3(const glm::vec3& deg) { return hpbToMat3(deg); }

glm::vec3 mat3ToEulerDeg(const glm::mat3& m) { return mat3ToHpb(m); }

// Proyecta un punto de mundo a pixeles de pantalla (mismo sistema de
// coordenadas que usa screenRay: origen arriba-izquierda, sin flip de Y --
// es el inverso exacto de como screenRay arma el NDC desde mouseX/mouseY,
// asi que el viaje de ida y vuelta es consistente). Devuelve false si el
// punto cae detras de la camara (no proyectable).
bool worldToScreen(const glm::vec3& p, const glm::mat4& viewProj, int width, int height, glm::vec2& outScreen) {
    const glm::vec4 clip = viewProj * glm::vec4(p, 1.0f);
    if (clip.w < 1e-4f) return false;
    const glm::vec3 ndc = glm::vec3(clip) / clip.w;
    outScreen = glm::vec2((ndc.x * 0.5f + 0.5f) * (float)width, (ndc.y * 0.5f + 0.5f) * (float)height);
    return true;
}

float pointToSegmentDist2D(const glm::vec2& p, const glm::vec2& a, const glm::vec2& b) {
    const glm::vec2 ab = b - a;
    const float len2 = glm::dot(ab, ab);
    const float t = len2 > 1e-8f ? std::clamp(glm::dot(p - a, ab) / len2, 0.0f, 1.0f) : 0.0f;
    return glm::distance(p, a + ab * t);
}

// Distancia minima en PIXELES del cursor a la polilinea del aro proyectada a
// pantalla. Reemplaza al viejo test "distancia radial en el plano 3D del
// aro": ese test media bien en el plano del aro, pero en vision rasante (la
// camara mirando el aro casi de canto -- le pasa a cualquiera de los 3 ejes
// segun el angulo, nunca al aro View por construccion) un desplazamiento de
// pocos pixeles en pantalla corresponde a un barrido enorme de angulo real
// sobre el aro, asi que la zona "dentro de banda" en 3D cubria una region
// de pantalla mucho mas grande que el aro dibujado -- de ahi que pareciera
// que CUALQUIER punto rotaba el objeto en modo Rotar (bug reportado 2026-10-30,
// el aro View nunca lo sufria porque siempre mira de frente a la camara).
// Medir en pixeles hace que el area clicable coincida con el dibujo sea cual
// sea el angulo de camara.
float ringScreenDist(const glm::vec3& center, const glm::vec3& axis, float radius,
                      const glm::mat4& viewProj, int width, int height,
                      const glm::vec2& cursor, int segments) {
    glm::vec3 u, v;
    basisForAxis(axis, u, v);
    float best = 1e30f;
    glm::vec2 prev{};
    bool prevValid = false;
    for (int i = 0; i <= segments; ++i) {
        const float a = (float)(i % segments) / (float)segments * glm::two_pi<float>();
        const glm::vec3 p3 = center + (u * std::cos(a) + v * std::sin(a)) * radius;
        glm::vec2 p2;
        const bool valid = worldToScreen(p3, viewProj, width, height, p2);
        if (valid && prevValid) best = std::min(best, pointToSegmentDist2D(cursor, prev, p2));
        prev = p2;
        prevValid = valid;
    }
    return best;
}

// Arcball de Shoemake: cursor de pantalla -> punto unitario sobre una
// semiesfera virtual (radio `radiusPixels`, centro `center`, orientada por
// la base `right`/`up`/`forward` -- `forward` apunta HACIA la camara). Fuera
// del circulo, el punto se proyecta al ecuador (z=0) en vez de extrapolar,
// que es la forma estandar de evitar que el arrastre se dispare al salir
// del circulo dibujado.
glm::vec3 arcballPoint(const glm::vec2& mouseScreen, const glm::vec2& center, float radiusPixels,
                        const glm::vec3& right, const glm::vec3& up, const glm::vec3& forward) {
    const float nx = (mouseScreen.x - center.x) / radiusPixels;
    const float ny = (mouseScreen.y - center.y) / radiusPixels; // pantalla: Y hacia abajo
    const float r2 = nx * nx + ny * ny;
    glm::vec3 local;
    if (r2 <= 1.0f) {
        local = glm::vec3(nx, -ny, std::sqrt(1.0f - r2)); // -ny: Y de pantalla hacia abajo -> Y de mundo hacia arriba
    } else {
        const float invLen = 1.0f / std::sqrt(r2);
        local = glm::vec3(nx * invLen, -ny * invLen, 0.0f);
    }
    return glm::normalize(right * local.x + up * local.y + forward * local.z);
}

// Punto dentro/fuera de la proyeccion a pantalla del circulo View (regla
// par-impar, "ray casting" estandar). Si algun punto muestreado del circulo
// cae detras de la camara se considera que el cursor esta FUERA (mas seguro
// que fiarse de una proyeccion invalida).
bool pointInProjectedRing(const glm::vec3& center, const glm::vec3& axis, float radius,
                           const glm::mat4& viewProj, int width, int height,
                           const glm::vec2& p, int segments) {
    glm::vec3 u, v;
    basisForAxis(axis, u, v);
    std::vector<glm::vec2> pts;
    pts.reserve(segments);
    for (int i = 0; i < segments; ++i) {
        const float a = (float)i / (float)segments * glm::two_pi<float>();
        const glm::vec3 p3 = center + (u * std::cos(a) + v * std::sin(a)) * radius;
        glm::vec2 p2;
        if (!worldToScreen(p3, viewProj, width, height, p2)) return false;
        pts.push_back(p2);
    }
    bool inside = false;
    for (size_t i = 0, j = pts.size() - 1; i < pts.size(); j = i++) {
        const glm::vec2& pi = pts[i];
        const glm::vec2& pj = pts[j];
        if (((pi.y > p.y) != (pj.y > p.y)) &&
            (p.x < (pj.x - pi.x) * (p.y - pi.y) / (pj.y - pi.y) + pi.x)) {
            inside = !inside;
        }
    }
    return inside;
}

void pushRing(std::vector<Vertex>& out, const glm::vec3& center, const glm::vec3& axis,
              float radius, const glm::vec3& color, int segments) {
    glm::vec3 u, v;
    basisForAxis(axis, u, v);
    for (int i = 0; i < segments; ++i) {
        float a0 = (float)i / (float)segments * glm::two_pi<float>();
        float a1 = (float)(i + 1) / (float)segments * glm::two_pi<float>();
        glm::vec3 p0 = center + (u * std::cos(a0) + v * std::sin(a0)) * radius;
        glm::vec3 p1 = center + (u * std::cos(a1) + v * std::sin(a1)) * radius;
        pushLine(out, p0, p1, color);
    }
}

} // namespace

// Donde se planta el gizmo: sobre el objeto, o en el centro comun cuando hay
// varios seleccionados (usePivot).
glm::vec3 TransformGizmo::originFor(const SceneNode* node) const {
    if (usePivot) return pivotWorld;
    return node ? glm::vec3(node->worldMatrix()[3]) : glm::vec3(0.0f);
}

Ray TransformGizmo::screenRay(double mouseX, double mouseY, int width, int height,
                               const glm::mat4& view, const glm::mat4& proj, const glm::vec3& eye) {
    const float ndcX = (float)(2.0 * mouseX / (double)width - 1.0);
    const float ndcY = (float)(2.0 * mouseY / (double)height - 1.0);
    const glm::mat4 invViewProj = glm::inverse(proj * view);

    glm::vec4 near4 = invViewProj * glm::vec4(ndcX, ndcY, 0.0f, 1.0f);
    glm::vec4 far4 = invViewProj * glm::vec4(ndcX, ndcY, 1.0f, 1.0f);
    near4 /= near4.w;
    far4 /= far4.w;

    Ray ray;
    ray.origin = eye;
    // Direccion entre dos puntos de la MISMA columna NDC (no entre `eye` y un
    // punto) -- asi da igual si el NDC z=0/1 corresponde al near/far real:
    // la recta que pasa por ambos puntos es la correcta de todos modos.
    ray.direction = glm::normalize(glm::vec3(far4) - glm::vec3(near4));
    // Proyeccion ortografica (proj[3][3] == 1): los rayos son paralelos y no
    // salen del ojo. El origen va sobre la recta de este pixel, bien atras,
    // para que tambien se puedan elegir objetos detras del ojo (vistas F2-F4).
    if (std::fabs(proj[3][3] - 1.0f) < 1e-6f) ray.origin = glm::vec3(near4) - ray.direction * 200.0f;
    return ray;
}

void TransformGizmo::axesFor(const SceneNode* node, glm::vec3 out[3]) const {
    for (int i = 0; i < 3; ++i) out[i] = kAxisDir[i];
    if (worldAxes || !node) return;
    const glm::mat3 m(node->worldMatrix());
    for (int i = 0; i < 3; ++i)
        if (glm::length(m[i]) > 1e-8f) out[i] = glm::normalize(m[i]);
}

namespace {
// Angulo equivalente (+-360) mas cercano a `ref`.
float nearestAngle(float a, float ref) { return a + 360.0f * std::round((ref - a) / 360.0f); }
} // namespace

void TransformGizmo::applyWorldRotation(const glm::mat3& d) {
    // Orientacion local L respecto al padre P: mundo = P*L. Girar en mundo
    // con d da P*L' = d*P*L, o sea L' = P^T * d * P * L.
    const glm::mat3 l = glm::transpose(parentRot_) * d * parentRot_ * eulerDegToMat3(startRotationDeg_);
    const glm::vec3 a = mat3ToEulerDeg(l);  // (P, H, B)
    const glm::vec3 cur = target_->rotationDeg;
    // Las dos soluciones H/P/B de la misma orientacion; se queda la mas
    // cercana a la actual (continuidad durante el arrastre).
    glm::vec3 s1(nearestAngle(a.x, cur.x), nearestAngle(a.y, cur.y), nearestAngle(a.z, cur.z));
    glm::vec3 s2(nearestAngle(180.0f - a.x, cur.x), nearestAngle(a.y + 180.0f, cur.y), nearestAngle(a.z + 180.0f, cur.z));
    const glm::vec3 d1 = glm::abs(s1 - cur), d2 = glm::abs(s2 - cur);
    target_->rotationDeg = (d2.x + d2.y + d2.z) + 1e-3f < (d1.x + d1.y + d1.z) ? s2 : s1;
}

GizmoAxis TransformGizmo::pickAxis(const Ray& ray, const SceneNode* node, const glm::vec3& eye,
                                    double mouseX, double mouseY, int viewportW, int viewportH,
                                    const glm::mat4& viewProj, float* outParam) const {
    if (!node) return GizmoAxis::None;
    const glm::vec3 originWorld = originFor(node);
    const float size = gizmoWorldSize(originWorld, eye);
    glm::vec3 ax[3];
    axesFor(node, ax);

    GizmoAxis best = GizmoAxis::None;
    float bestScore = 1e30f;
    float bestParam = 0.0f;

    if (mode == GizmoMode::Translate || mode == GizmoMode::Scale) {
        const float threshold = size * 0.12f;
        for (int i = 0; i < 3; ++i) {
            const glm::vec3& dir = ax[i];
            const float t = closestLineParam(ray, originWorld, dir);
            if (t < -size * 0.1f || t > size * 1.3f) continue; // fuera del segmento visible del eje
            const glm::vec3 pointOnLine = originWorld + dir * t;
            // Distancia del rayo a ese punto (no al origen del rayo): mas
            // fiel a "donde esta el cursor en pantalla" que distancia 3D cruda.
            const glm::vec3 toPoint = pointOnLine - ray.origin;
            const glm::vec3 closestOnRay = ray.origin + ray.direction * glm::dot(toPoint, ray.direction);
            const float dist = glm::distance(pointOnLine, closestOnRay);
            if (dist < threshold && dist < bestScore) {
                bestScore = dist;
                best = axisFromIndex(i);
                bestParam = t;
            }
        }
        // Clic fuera de las 3 flechas/handles: modo Free (ver comentario de
        // GizmoAxis en el .h) en vez de caer a seleccion -- beginDrag congela
        // su propia referencia de arranque (plano de vista para Mover,
        // distancia en pantalla para Escalar), bestParam no se usa ahi.
        if (best == GizmoAxis::None) best = GizmoAxis::Free;
    } else { // Rotate -- picking de los 3 aros de eje en PIXELES de pantalla,
             // no en el plano 3D del aro (ver ringScreenDist arriba: evita el
             // "area clicable gigante" en vision rasante). Si no cae en
             // ninguno, decide Free/View igual que C4D (ver comentario de
             // GizmoAxis en el .h): dentro del circulo -> Free, fuera -> View.
        constexpr float kPickPixels = 10.0f;
        const glm::vec2 cursor((float)mouseX, (float)mouseY);
        float bestPixelDist = kPickPixels;
        int bestAxisIdx = -1;

        for (int i = 0; i < 3; ++i) {
            const float d = ringScreenDist(originWorld, ax[i], size, viewProj, viewportW, viewportH,
                                            cursor, kRingSegments);
            if (d < bestPixelDist) {
                bestPixelDist = d;
                bestAxisIdx = i;
            }
        }

        if (bestAxisIdx >= 0) {
            const glm::vec3& dir = ax[bestAxisIdx];
            glm::vec3 hit;
            if (rayPlaneIntersect(ray, originWorld, dir, hit)) {
                glm::vec3 u, v;
                basisForAxis(dir, u, v);
                const glm::vec3 local = hit - originWorld;
                best = axisFromIndex(bestAxisIdx);
                bestParam = std::atan2(glm::dot(local, v), glm::dot(local, u));
            }
        } else {
            const glm::vec3 viewAxis = glm::normalize(eye - originWorld);
            const float viewRadius = size * 1.3f;
            const bool inside = pointInProjectedRing(originWorld, viewAxis, viewRadius, viewProj, viewportW,
                                                       viewportH, cursor, kRingSegments);
            // El angulo/parametro no se calcula aqui en ninguno de los dos
            // casos: beginDrag congela su propia base al empezar el
            // arrastre de verdad (viewAxisWorld_/viewBasisU_/V_ para View,
            // freeCamRight_/freeCamUp_/freeStartMouse para Free).
            best = inside ? GizmoAxis::Free : GizmoAxis::View;
            bestParam = 0.0f;
        }
    }

    if (outParam) *outParam = bestParam;
    return best;
}

GizmoAxis TransformGizmo::pickHandle(const Ray& ray, const SceneNode* node, const glm::vec3& eye,
                                     double mouseX, double mouseY, int viewportW, int viewportH,
                                     const glm::mat4& viewProj) const {
    const GizmoAxis a = pickAxis(ray, node, eye, mouseX, mouseY, viewportW, viewportH, viewProj, nullptr);
    if (a == GizmoAxis::X || a == GizmoAxis::Y || a == GizmoAxis::Z) return a;
    if (a == GizmoAxis::View && node) {
        // Fuera del circulo: solo cuenta como handle cerca del aro de vista.
        const glm::vec3 originWorld = originFor(node);
        const float size = gizmoWorldSize(originWorld, eye);
        const glm::vec3 viewAxis = glm::normalize(eye - originWorld);
        const float d = ringScreenDist(originWorld, viewAxis, size * 1.3f, viewProj, viewportW, viewportH,
                                       glm::vec2((float)mouseX, (float)mouseY), kRingSegments);
        if (d < 10.0f) return GizmoAxis::View;
    }
    return GizmoAxis::None;
}

void TransformGizmo::updateHover(const Ray& ray, const SceneNode* node, const glm::vec3& eye,
                                  double mouseX, double mouseY, int viewportW, int viewportH,
                                  const glm::mat4& viewProj) {
    if (dragging_) return;
    float unused = 0.0f;
    hovered_ = pickAxis(ray, node, eye, mouseX, mouseY, viewportW, viewportH, viewProj, &unused);
}

bool TransformGizmo::beginDrag(const Ray& ray, SceneNode* node, const glm::vec3& eye,
                                double mouseX, double mouseY, int viewportW, int viewportH,
                                const glm::mat4& viewProj, const glm::vec3& camRight, const glm::vec3& camUp,
                                GizmoAxis force) {
    if (!node) return false;
    float param = 0.0f;
    const GizmoAxis picked = pickAxis(ray, node, eye, mouseX, mouseY, viewportW, viewportH, viewProj, &param);
    const GizmoAxis axis = force != GizmoAxis::None ? force : picked;
    if (axis == GizmoAxis::None) return false;
    if (axis != picked) param = 0.0f;  // Free/View no usan el parametro

    target_ = node;
    active_ = axis;
    dragging_ = true;
    originWorld_ = originFor(node);
    // Un desplazamiento de mundo pasa al espacio del padre con la INVERSA de
    // su 3x3 (antes era la inversa-traspuesta, que para una rotacion pura es
    // la propia rotacion: con un padre girado el hijo se movia en otra
    // direccion que la flecha).
    {
        const glm::mat3 pm(node->parentWorldMatrix());
        parentInverseRot_ = std::fabs(glm::determinant(pm)) > 1e-12f ? glm::inverse(pm) : glm::mat3(1.0f);
        parentRot_ = glm::mat3(1.0f);
        for (int i = 0; i < 3; ++i)
            if (glm::length(pm[i]) > 1e-8f) parentRot_[i] = glm::normalize(pm[i]);
    }
    axesFor(node, axes_);
    startPosition_ = node->position;
    startRotationDeg_ = node->rotationDeg;
    startScale_ = node->scale;

    if (mode == GizmoMode::Rotate) {
        if (axis == GizmoAxis::View) {
            // Congela eje y base angular AQUI -- updateDrag ya no tiene
            // acceso a `eye`, asi que esto es lo unico que se usara durante
            // todo el arrastre, nunca recalculado.
            viewAxisWorld_ = glm::normalize(eye - originWorld_);
            basisForAxis(viewAxisWorld_, viewBasisU_, viewBasisV_);
            glm::vec3 hit;
            startAngle_ = 0.0f;
            if (rayPlaneIntersect(ray, originWorld_, viewAxisWorld_, hit)) {
                const glm::vec3 local = hit - originWorld_;
                startAngle_ = std::atan2(glm::dot(local, viewBasisV_), glm::dot(local, viewBasisU_));
            }
        } else if (axis == GizmoAxis::Free) {
            // Congela base de camara, eje forward (reusa viewAxisWorld_, el
            // mismo circulo que View), centro/radio en pantalla del circulo
            // View y el punto de arranque sobre la semiesfera -- igual
            // criterio que el anillo View: updateDrag no recibe `eye` ni la
            // camara, asi que esto es lo unico que se usa durante todo el
            // arrastre.
            freeCamRight_ = camRight;
            freeCamUp_ = camUp;
            viewAxisWorld_ = glm::normalize(eye - originWorld_);
            const float size = gizmoWorldSize(originWorld_, eye);
            glm::vec2 centerScreen{}, edgeScreen{};
            const bool okC = worldToScreen(originWorld_, viewProj, viewportW, viewportH, centerScreen);
            const glm::vec3 edgeWorld = originWorld_ + camRight * (size * 1.3f);
            const bool okE = worldToScreen(edgeWorld, viewProj, viewportW, viewportH, edgeScreen);
            freeScreenCenter_ = okC ? centerScreen : glm::vec2((float)mouseX, (float)mouseY);
            freeScreenRadius_ = (okC && okE) ? std::max(glm::distance(centerScreen, edgeScreen), 1.0f) : 1.0f;
            freeStartPoint_ = arcballPoint(glm::vec2((float)mouseX, (float)mouseY), freeScreenCenter_,
                                            freeScreenRadius_, freeCamRight_, freeCamUp_, viewAxisWorld_);
        } else {
            startAngle_ = param;
        }
    } else if (axis == GizmoAxis::Free) {
        // Mover/Escalar en modo libre (clic fuera de cualquier flecha/handle
        // de eje, ver pickAxis): ambos anclan el plano de vista (mismo eje
        // camara-objeto que el anillo View de Rotar) y el origen del gizmo
        // proyectado a pantalla -- updateDrag no recibe `eye` ni la camara,
        // asi que esto es lo unico que se usa durante todo el arrastre.
        viewAxisWorld_ = glm::normalize(eye - originWorld_);
        glm::vec2 originScreen{};
        const bool okOrigin = worldToScreen(originWorld_, viewProj, viewportW, viewportH, originScreen);
        freeScreenCenter_ = okOrigin ? originScreen : glm::vec2((float)mouseX, (float)mouseY);
        if (mode == GizmoMode::Translate) {
            glm::vec3 hit;
            freeDragStartWorld_ =
                rayPlaneIntersect(ray, originWorld_, viewAxisWorld_, hit) ? hit : originWorld_;
        } else { // Scale: distancia inicial en pantalla del cursor al origen del gizmo
            startParam_ =
                std::max(glm::distance(glm::vec2((float)mouseX, (float)mouseY), freeScreenCenter_), 1.0f);
        }
    } else {
        startParam_ = param;
    }
    return true;
}

void TransformGizmo::updateDrag(const Ray& ray, double mouseX, double mouseY) {
    if (!dragging_ || !target_) return;

    if (mode == GizmoMode::Rotate && active_ == GizmoAxis::View) {
        glm::vec3 hit;
        if (!rayPlaneIntersect(ray, originWorld_, viewAxisWorld_, hit)) return;
        const glm::vec3 local = hit - originWorld_;
        const float angle = std::atan2(glm::dot(local, viewBasisV_), glm::dot(local, viewBasisU_));
        const float deltaDeg = glm::degrees(angle - startAngle_);
        // Rotacion compuesta (no se puede escribir en un solo componente
        // Euler como con X/Y/Z): delta alrededor del eje de vista congelado,
        // aplicada ENCIMA de la rotacion que tenia el objeto al empezar.
        applyWorldRotation(glm::mat3(glm::rotate(glm::mat4(1.0f), glm::radians(deltaDeg), viewAxisWorld_)));
        return;
    }

    if (mode == GizmoMode::Rotate && active_ == GizmoAxis::Free) {
        // Arcball de Shoemake (ver comentario junto a arcballPoint() y a los
        // campos free* en el .h): rotacion minima que lleva el punto de
        // arranque sobre la semiesfera al punto actual bajo el cursor.
        // Escala automaticamente con el radio en pantalla del circulo View
        // -- ni magic numbers ni deriva frame a frame, el delta siempre se
        // mide contra el arranque del arrastre.
        const glm::vec3 p1 = arcballPoint(glm::vec2((float)mouseX, (float)mouseY), freeScreenCenter_,
                                           freeScreenRadius_, freeCamRight_, freeCamUp_, viewAxisWorld_);
        const float cosAngle = std::clamp(glm::dot(freeStartPoint_, p1), -1.0f, 1.0f);
        const float angle = std::acos(cosAngle);
        if (angle > 1e-5f) {
            glm::vec3 axisWorld = glm::cross(freeStartPoint_, p1);
            const float axisLen = glm::length(axisWorld);
            if (axisLen > 1e-6f) {
                axisWorld /= axisLen;
                applyWorldRotation(glm::mat3(glm::rotate(glm::mat4(1.0f), angle, axisWorld)));
            }
        }
        return;
    }

    if (mode == GizmoMode::Translate && active_ == GizmoAxis::Free) {
        // Mover libre: arrastre sobre el plano de vista congelado en
        // beginDrag (mismo eje camara-objeto que el anillo View de Rotar).
        glm::vec3 hit;
        if (!rayPlaneIntersect(ray, originWorld_, viewAxisWorld_, hit)) return;
        const glm::vec3 worldDelta = hit - freeDragStartWorld_;
        const glm::vec3 localDelta = parentInverseRot_ * worldDelta;
        target_->position = startPosition_ + localDelta;
        return;
    }

    if (mode == GizmoMode::Scale && active_ == GizmoAxis::Free) {
        // Escalar libre: uniforme en los 3 ejes segun la razon entre la
        // distancia en pantalla actual y la congelada en beginDrag, mismos
        // limites que el escalado por eje individual (ver mas abajo).
        const float currentDist = glm::distance(glm::vec2((float)mouseX, (float)mouseY), freeScreenCenter_);
        const float ratio = std::clamp(currentDist / std::max(startParam_, 1.0f), 0.02f, 50.0f);
        glm::vec3 newScale;
        newScale.x = std::clamp(startScale_.x * ratio, 0.01f, 1000.0f);
        newScale.y = std::clamp(startScale_.y * ratio, 0.01f, 1000.0f);
        newScale.z = std::clamp(startScale_.z * ratio, 0.01f, 1000.0f);
        target_->scale = newScale;
        return;
    }

    const int idx = axisIndex(active_);
    const glm::vec3& dir = axes_[idx];

    if (mode == GizmoMode::Translate) {
        const float t = closestLineParam(ray, originWorld_, dir);
        const glm::vec3 worldDelta = dir * (t - startParam_);
        const glm::vec3 localDelta = parentInverseRot_ * worldDelta;
        target_->position = startPosition_ + localDelta;

    } else if (mode == GizmoMode::Scale) {
        const float t = closestLineParam(ray, originWorld_, dir);
        const float minParam = 0.01f;
        const float startAbs = std::max(std::abs(startParam_), minParam);
        const float ratio = std::clamp(std::abs(t) / startAbs, 0.02f, 50.0f);
        glm::vec3 newScale = startScale_;
        newScale[idx] = std::clamp(startScale_[idx] * ratio, 0.01f, 1000.0f);
        target_->scale = newScale;

    } else { // Rotate
        glm::vec3 hit;
        if (!rayPlaneIntersect(ray, originWorld_, dir, hit)) return;
        glm::vec3 u, v;
        basisForAxis(dir, u, v);
        const glm::vec3 local = hit - originWorld_;
        const float angle = std::atan2(glm::dot(local, v), glm::dot(local, u));
        const float deltaDeg = glm::degrees(angle - startAngle_);
        applyWorldRotation(glm::mat3(glm::rotate(glm::mat4(1.0f), glm::radians(deltaDeg), dir)));
    }
}

void TransformGizmo::endDrag() {
    dragging_ = false;
    active_ = GizmoAxis::None;
    target_ = nullptr;
}

void TransformGizmo::buildGeometry(const SceneNode* node, const glm::vec3& eye,
                                    std::vector<Vertex>& outLines, std::vector<Vertex>& outTriangles) const {
    outLines.clear();
    outTriangles.clear();
    if (!node) return;

    const glm::vec3 originWorld = originFor(node);
    const float size = gizmoWorldSize(originWorld, eye);
    const GizmoAxis shown = dragging_ ? active_ : hovered_;
    glm::vec3 ax[3];
    if (dragging_) for (int i = 0; i < 3; ++i) ax[i] = axes_[i];
    else axesFor(node, ax);

    for (int i = 0; i < 3; ++i) {
        const glm::vec3& dir = ax[i];
        const bool isShown = (shown == axisFromIndex(i));
        const glm::vec3 color = isShown ? kHighlightColor : kAxisColor[i];

        if (mode == GizmoMode::Translate) {
            const glm::vec3 shaftEnd = originWorld + dir * size * 0.78f;
            pushLine(outLines, originWorld, shaftEnd, color);
            pushCone(outTriangles, shaftEnd, originWorld + dir * size, size * 0.055f, color, kConeSegments);
        } else if (mode == GizmoMode::Scale) {
            const glm::vec3 shaftEnd = originWorld + dir * size * 0.82f;
            pushLine(outLines, originWorld, shaftEnd, color);
            pushCube(outTriangles, originWorld + dir * size * 0.9f, size * 0.05f, color);
        } else { // Rotate
            pushRing(outLines, originWorld, dir, size, color, kRingSegments);
        }
    }

    if (mode == GizmoMode::Rotate) {
        // A diferencia de los 3 anillos de eje, el anillo de vista de C4D NO
        // cambia de color al interactuar con el (confirmado visualmente en
        // capturas reales, 28 oct) -- siempre el mismo gris, sea cual sea
        // `shown`.
        const glm::vec3 viewAxis = glm::normalize(eye - originWorld);
        pushRing(outLines, originWorld, viewAxis, size * 1.3f, kViewRingColor, kRingSegments);
    }
}

} // namespace iron3d
