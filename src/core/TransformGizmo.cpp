#include "TransformGizmo.h"
#include "SceneNode.h"

#include <glm/gtc/matrix_inverse.hpp>
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
    return ray;
}

GizmoAxis TransformGizmo::pickAxis(const Ray& ray, const SceneNode* node, const glm::vec3& eye,
                                    float* outParam) const {
    if (!node) return GizmoAxis::None;
    const glm::vec3 originWorld = glm::vec3(node->worldMatrix()[3]);
    const float size = gizmoWorldSize(originWorld, eye);

    GizmoAxis best = GizmoAxis::None;
    float bestScore = 1e30f;
    float bestParam = 0.0f;

    if (mode == GizmoMode::Translate || mode == GizmoMode::Scale) {
        const float threshold = size * 0.12f;
        for (int i = 0; i < 3; ++i) {
            const glm::vec3& dir = kAxisDir[i];
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
    } else { // Rotate
        const float band = size * 0.18f;
        for (int i = 0; i < 3; ++i) {
            const glm::vec3& dir = kAxisDir[i];
            glm::vec3 hit;
            if (!rayPlaneIntersect(ray, originWorld, dir, hit)) continue;
            const float r = glm::distance(hit, originWorld);
            const float dist = std::abs(r - size);
            if (dist < band && dist < bestScore) {
                glm::vec3 u, v;
                basisForAxis(dir, u, v);
                const glm::vec3 local = hit - originWorld;
                bestScore = dist;
                best = axisFromIndex(i);
                bestParam = std::atan2(glm::dot(local, v), glm::dot(local, u));
            }
        }
    }

    if (outParam) *outParam = bestParam;
    return best;
}

void TransformGizmo::updateHover(const Ray& ray, const SceneNode* node, const glm::vec3& eye) {
    if (dragging_) return;
    float unused = 0.0f;
    hovered_ = pickAxis(ray, node, eye, &unused);
}

bool TransformGizmo::beginDrag(const Ray& ray, SceneNode* node, const glm::vec3& eye) {
    if (!node) return false;
    float param = 0.0f;
    const GizmoAxis axis = pickAxis(ray, node, eye, &param);
    if (axis == GizmoAxis::None) return false;

    target_ = node;
    active_ = axis;
    dragging_ = true;
    originWorld_ = glm::vec3(node->worldMatrix()[3]);
    parentInverseRot_ = glm::inverseTranspose(glm::mat3(node->parentWorldMatrix()));
    // inverseTranspose deshace solo la parte de ROTACION de forma estable
    // incluso si el padre tiene escala -- para convertir un delta de
    // DIRECCION de mundo a espacio del padre (no un punto), que es lo que
    // necesitamos aqui, el inverso simple ya sirve en el caso sin escala no
    // uniforme; se usa inverseTranspose por ser la forma robusta estandar.
    startPosition_ = node->position;
    startRotationDeg_ = node->rotationDeg;
    startScale_ = node->scale;

    if (mode == GizmoMode::Rotate) {
        startAngle_ = param;
    } else {
        startParam_ = param;
    }
    return true;
}

void TransformGizmo::updateDrag(const Ray& ray) {
    if (!dragging_ || !target_) return;
    const int idx = axisIndex(active_);
    const glm::vec3& dir = kAxisDir[idx];

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
        glm::vec3 newRot = startRotationDeg_;
        newRot[idx] = startRotationDeg_[idx] + deltaDeg;
        target_->rotationDeg = newRot;
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

    const glm::vec3 originWorld = glm::vec3(node->worldMatrix()[3]);
    const float size = gizmoWorldSize(originWorld, eye);
    const GizmoAxis shown = dragging_ ? active_ : hovered_;

    for (int i = 0; i < 3; ++i) {
        const glm::vec3& dir = kAxisDir[i];
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
}

} // namespace iron3d
