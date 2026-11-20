#include "Clone.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

namespace iron3d {

namespace {
constexpr size_t kMaxVertices = 2000000;
constexpr float kCmPerUnitClone = 200.0f;

bool sameParams(const CloneData& c) {
    return c.lastMode == c.mode && c.lastCount == c.count && c.lastCountY == c.countY && c.lastCountZ == c.countZ &&
           c.lastOffset == c.offset && c.lastRadius == c.radiusCm && c.lastAxis == c.axis &&
           c.lastScaleStep == c.scaleStepPct && c.lastRotStep == c.rotStepDeg;
}
} // namespace

int cloneCount(const CloneData& c) {
    long n = 1;
    if (c.mode == 1) n = (long)std::max(1, c.count) * std::max(1, c.countY) * std::max(1, c.countZ);
    else n = std::max(1, c.count);
    return (int)std::min<long>(n, kMaxClones);
}

MeshData cloneMeshData(const MeshData& src, const CloneData& c, const glm::mat4& toParent) {
    MeshData out;
    const int total = cloneCount(c);
    const size_t perV = src.vertices.size();
    if (!perV || src.indices.empty()) return out;
    const int copies = (int)std::min<size_t>((size_t)total, kMaxVertices / perV);
    if (copies <= 0) return out;
    const glm::mat3 nbase = glm::transpose(glm::inverse(glm::mat3(toParent)));
    out.vertices.reserve(perV * copies);
    out.indices.reserve(src.indices.size() * copies);
    out.faceTris.reserve(src.faceTris.size() * copies);
    const glm::vec3 off = c.offset / kCmPerUnitClone;
    const int cx = std::max(1, c.count), cy = std::max(1, c.countY), cz = std::max(1, c.countZ);
    const int k = std::clamp(c.axis, 0, 2);
    const int u = (k + 1) % 3, v = (k + 2) % 3;  // u x v = eje k: la rotacion de a sobre k lleva e_u hacia e_v
    for (int i = 0; i < copies; ++i) {
        glm::mat4 t(1.0f);
        float ang = 0.0f;
        glm::vec3 axisVec(0.0f);
        glm::vec3 pos(0.0f);
        if (c.mode == 1) {  // cuadricula centrada
            const int ix = i % cx, iy = (i / cx) % cy, iz = i / (cx * cy);
            pos = glm::vec3((ix - (cx - 1) * 0.5f) * off.x, (iy - (cy - 1) * 0.5f) * off.y, (iz - (cz - 1) * 0.5f) * off.z);
            axisVec = glm::vec3(0, 1, 0);
            ang = glm::radians(c.rotStepDeg) * (float)i;
        } else if (c.mode == 2) {  // radial: las copias miran hacia fuera
            const float a = 6.28318530718f * (float)i / (float)std::max(1, copies);
            const float r = c.radiusCm / kCmPerUnitClone;
            glm::vec3 eu(0.0f), ev(0.0f);
            eu[u] = 1.0f;
            ev[v] = 1.0f;
            pos = r * (std::cos(a) * eu + std::sin(a) * ev);
            axisVec = glm::vec3(0.0f);
            axisVec[k] = 1.0f;
            ang = a + glm::radians(c.rotStepDeg) * (float)i;
        } else {  // lineal
            pos = off * (float)i;
            axisVec = glm::vec3(0, 1, 0);
            ang = glm::radians(c.rotStepDeg) * (float)i;
        }
        const float s = std::max(0.01f, 1.0f + c.scaleStepPct / 100.0f * (float)i);
        t = glm::translate(glm::mat4(1.0f), pos);
        if (std::fabs(ang) > 1e-9f) t = glm::rotate(t, ang, axisVec);
        t = glm::scale(t, glm::vec3(s));
        const glm::mat4 xf = t * toParent;
        const glm::mat3 nxf = glm::transpose(glm::inverse(glm::mat3(xf)));
        const uint32_t base = (uint32_t)out.vertices.size();
        for (Vertex vx : src.vertices) {
            vx.position = glm::vec3(xf * glm::vec4(vx.position, 1.0f));
            const glm::vec3 nn = nxf * vx.normal;
            vx.normal = glm::length(nn) > 1e-8f ? glm::normalize(nn) : vx.normal;
            out.vertices.push_back(vx);
        }
        (void)nbase;
        for (uint32_t ix : src.indices) out.indices.push_back(base + ix);
        out.faceTris.insert(out.faceTris.end(), src.faceTris.begin(), src.faceTris.end());
    }
    updateBounds(out);
    return out;
}

namespace {
SceneNode* firstMeshChild(SceneNode& node) {
    for (const auto& c : node.children())
        if (c->mesh && !c->clone.present) return c.get();
    return nullptr;
}

void walk(SceneNode& node, int& count) {
    for (const auto& c : node.children()) walk(*c, count);  // primero los hijos: un clonador anidado ya esta al dia
    if (!node.clone.present) return;
    CloneData& d = node.clone;
    if (SceneNode* child = firstMeshChild(node)) {
        const glm::mat4 cm = child->localMatrix();
        if (d.lastSource == child->mesh && d.lastChildMatrix == cm && sameParams(d) && node.mesh == d.output) return;
        auto m = std::make_shared<const MeshData>(cloneMeshData(*child->mesh, d, cm));
        node.mesh = m;
        node.primitive = PrimitiveType::Mesh;
        node.materialIndex = child->materialIndex;
        node.texTag = child->texTag;
        d.output = m;
        d.lastSource = child->mesh;
        d.lastChildMatrix = cm;
        d.lastMode = d.mode; d.lastCount = d.count; d.lastCountY = d.countY; d.lastCountZ = d.countZ;
        d.lastOffset = d.offset; d.lastRadius = d.radiusCm; d.lastAxis = d.axis;
        d.lastScaleStep = d.scaleStepPct; d.lastRotStep = d.rotStepDeg;
        ++count;
    } else if (node.mesh) {
        node.mesh.reset();
        d.output.reset();
        d.lastSource.reset();
    }
}
} // namespace

int evaluateCloners(SceneNode& root) {
    int n = 0;
    walk(root, n);
    return n;
}

} // namespace iron3d
