#include "Clone.h"
#include "Symmetry.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <array>
#include <set>
#include <cmath>

namespace iron3d {

namespace {
constexpr size_t kMaxVertices = 2000000;
constexpr float kCmPerUnitClone = 200.0f;

// Numero al azar en [-1, 1] que solo depende de la copia, el canal y la semilla (reproducible).
float rand11(int i, int channel, int seed) {
    uint32_t x = (uint32_t)i * 2654435761u ^ (uint32_t)channel * 40503u ^ (uint32_t)seed * 2246822519u;
    x ^= x >> 15; x *= 2246822519u; x ^= x >> 13; x *= 3266489917u; x ^= x >> 16;
    return (float)(x & 0xFFFFFF) / (float)0x7FFFFF - 1.0f;
}

bool sameParams(const CloneData& c) {
    return c.lastMode == c.mode && c.lastCount == c.count && c.lastCountY == c.countY && c.lastCountZ == c.countZ &&
           c.lastOffset == c.offset && c.lastRadius == c.radiusCm && c.lastAxis == c.axis &&
           c.lastScaleStep == c.scaleStepPct && c.lastRotStep == c.rotStepDeg && c.lastRandPos == c.randPosCm &&
           c.lastRandRot == c.randRotDeg && c.lastRandScale == c.randScalePct && c.lastSeed == c.seed;
}
} // namespace

int cloneCount(const CloneData& c) {
    long n = 1;
    if (c.mode == 1) n = (long)std::max(1, c.count) * std::max(1, c.countY) * std::max(1, c.countZ);
    else n = std::max(1, c.count);
    return (int)std::min<long>(n, kMaxClones);
}

std::vector<glm::vec3> cloneTargetPoints(const MeshData& m, const glm::mat4& toParent) {
    std::vector<glm::vec3> pts;
    std::set<std::array<long, 3>> seen;
    for (const Vertex& v : m.vertices) {
        const glm::vec3 p = glm::vec3(toParent * glm::vec4(v.position, 1.0f));
        const std::array<long, 3> key = {std::lround(p.x * 10000.0f), std::lround(p.y * 10000.0f), std::lround(p.z * 10000.0f)};
        if (seen.insert(key).second) {
            pts.push_back(p);
            if ((int)pts.size() >= kMaxClones) break;
        }
    }
    return pts;
}

MeshData cloneMeshData(const MeshData& src, const CloneData& c, const glm::mat4& toParent, const std::vector<glm::vec3>* points) {
    MeshData out;
    const int total = c.mode == 3 ? (points ? (int)std::min<size_t>(points->size(), kMaxClones) : 0) : cloneCount(c);
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
        } else if (c.mode == 3) {  // objeto: una copia en cada punto del otro objeto
            pos = (*points)[i];
            axisVec = glm::vec3(0, 1, 0);
            ang = glm::radians(c.rotStepDeg) * (float)i;
        } else {  // lineal
            pos = off * (float)i;
            axisVec = glm::vec3(0, 1, 0);
            ang = glm::radians(c.rotStepDeg) * (float)i;
        }
        pos += glm::vec3(rand11(i, 0, c.seed) * c.randPosCm.x, rand11(i, 1, c.seed) * c.randPosCm.y, rand11(i, 2, c.seed) * c.randPosCm.z) / kCmPerUnitClone;
        ang += glm::radians(c.randRotDeg) * rand11(i, 3, c.seed);
        const float s = std::max(0.01f, (1.0f + c.scaleStepPct / 100.0f * (float)i) * (1.0f + c.randScalePct / 100.0f * rand11(i, 4, c.seed)));
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

SceneNode* secondMeshChild(SceneNode& node, const SceneNode* first) {
    for (const auto& c : node.children())
        if (c.get() != first && c->mesh && !c->clone.present) return c.get();
    return nullptr;
}


MeshData passThroughMesh(const MeshData& src, const glm::mat4& cm) {
    MeshData out = src;
    const glm::mat3 nm = glm::transpose(glm::inverse(glm::mat3(cm)));
    for (Vertex& v : out.vertices) {
        v.position = glm::vec3(cm * glm::vec4(v.position, 1.0f));
        v.normal = glm::normalize(nm * v.normal);
    }
    updateBounds(out);
    return out;
}

void walk(SceneNode& node, int& count) {
    for (const auto& c : node.children()) walk(*c, count);  // primero los hijos: un clonador anidado ya esta al dia
    if (!node.clone.present) return;
    CloneData& d = node.clone;
    bakeParametricChildren(node);
    if (SceneNode* child = firstMeshChild(node); child && !g_useGenerators) {
        const glm::mat4 cm = child->localMatrix();
        if (d.lastMode == -1 && d.lastSource == child->mesh && d.lastChildMatrix == cm && node.mesh == d.output) return;
        auto m = std::make_shared<const MeshData>(passThroughMesh(*child->mesh, cm));
        node.mesh = m;
        node.primitive = PrimitiveType::Mesh;
        d.output = m;
        d.lastSource = child->mesh;
        d.lastChildMatrix = cm;
        d.lastMode = -1;  // fuerza el recalculo al volver a activar los generadores
        ++count;
        return;
    }
    if (SceneNode* child = firstMeshChild(node)) {
        const glm::mat4 cm = child->localMatrix();
        SceneNode* other = d.mode == 3 ? secondMeshChild(node, child) : nullptr;
        const glm::mat4 cm2 = other ? other->localMatrix() : glm::mat4(1.0f);
        const std::shared_ptr<const MeshData> src2 = other ? other->mesh : nullptr;
        if (d.lastSource == child->mesh && d.lastChildMatrix == cm && sameParams(d) && node.mesh == d.output &&
            d.lastSource2 == src2 && d.lastMatrix2 == cm2)
            return;
        std::vector<glm::vec3> pts;
        if (other) pts = cloneTargetPoints(*other->mesh, cm2);
        auto m = std::make_shared<const MeshData>(cloneMeshData(*child->mesh, d, cm, d.mode == 3 ? &pts : nullptr));
        d.lastSource2 = src2;
        d.lastMatrix2 = cm2;
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
        d.lastRandPos = d.randPosCm; d.lastRandRot = d.randRotDeg; d.lastRandScale = d.randScalePct; d.lastSeed = d.seed;
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
