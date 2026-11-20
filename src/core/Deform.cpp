#include "Deform.h"

#include <algorithm>
#include <cmath>

namespace iron3d {

MeshData deformMeshData(const MeshData& src, int kind, int axis, float amount, const glm::mat4& toParent) {
    const int a = std::clamp(axis, 0, 2), b = (a + 1) % 3, c = (a + 2) % 3;
    MeshData out;
    out.vertices = src.vertices;
    out.indices = src.indices;
    out.faceTris = src.faceTris;
    const glm::mat3 nm = glm::mat3(toParent);
    float lo = 1e30f, hi = -1e30f;
    for (Vertex& v : out.vertices) {
        v.position = glm::vec3(toParent * glm::vec4(v.position, 1.0f));
        v.normal = nm * v.normal;
        if (glm::length(v.normal) > 1e-12f) v.normal = glm::normalize(v.normal);
        lo = std::min(lo, v.position[a]);
        hi = std::max(hi, v.position[a]);
    }
    if (out.vertices.empty()) return out;
    const float len = std::max(hi - lo, 1e-6f), mid = 0.5f * (lo + hi);
    for (Vertex& v : out.vertices) {
        glm::vec3& p = v.position;
        glm::vec3& n = v.normal;
        switch (kind) {
            case 0: {  // Doblar: arco de `amount` grados en todo el largo, hacia +b
                const float ang = glm::radians(std::clamp(amount, -360.0f, 360.0f));
                if (std::fabs(ang) < 1e-5f) break;
                const float k = ang / len, R = 1.0f / k;
                const float phi = (p[a] - mid) * k, d = p[b];
                const float s = std::sin(phi), co = std::cos(phi);
                p[a] = mid + (R - d) * s;
                p[b] = R - (R - d) * co;
                const float na = n[a], nb = n[b];
                n[a] = na * co - nb * s;
                n[b] = na * s + nb * co;
                break;
            }
            case 1: {  // Enroscar: giro alrededor del eje, `amount` grados en todo el largo
                const float th = glm::radians(amount) * (p[a] - mid) / len;
                const float s = std::sin(th), co = std::cos(th);
                const float pb = p[b], pc = p[c], nb = n[b], nc = n[c];
                p[b] = pb * co - pc * s;
                p[c] = pb * s + pc * co;
                n[b] = nb * co - nc * s;
                n[c] = nb * s + nc * co;
                break;
            }
            case 2: {  // Afilar: la seccion se encoge hacia el final del eje (100 % = punta)
                const float f = 1.0f - (amount / 100.0f) * ((p[a] - lo) / len);
                p[b] *= f;
                p[c] *= f;
                break;
            }
            case 3: {  // Hinchar: seccion mayor en el medio
                const float f = 1.0f + (amount / 100.0f) * std::sin(3.14159265f * ((p[a] - lo) / len));
                p[b] *= f;
                p[c] *= f;
                break;
            }
            case 4: {  // Sesgar: desplaza b en proporcion a la posicion en el eje
                const float s = amount / 100.0f;
                p[b] += s * (p[a] - mid);
                n[a] -= s * n[b];  // normales: inversa traspuesta
                break;
            }
            default: break;
        }
        if (glm::length(n) > 1e-12f) n = glm::normalize(n);
    }
    updateBounds(out);
    return out;
}

namespace {
SceneNode* firstMeshChild(SceneNode& node) {
    for (const auto& c : node.children())
        if (c->mesh && !c->symmetry.present && !c->deform.present) return c.get();
    return nullptr;
}

void walk(SceneNode& node, int& count) {
    if (node.deform.present) {
        DeformData& d = node.deform;
        if (SceneNode* child = firstMeshChild(node)) {
            const glm::mat4 cm = child->localMatrix();
            const bool same = d.lastSource == child->mesh && d.lastChildMatrix == cm && d.lastKind == d.kind &&
                              d.lastAxis == d.axis && d.lastAmount == d.amount && node.mesh == d.output;
            if (!same) {
                auto m = std::make_shared<const MeshData>(deformMeshData(*child->mesh, d.kind, d.axis, d.amount, cm));
                node.mesh = m;
                node.primitive = PrimitiveType::Mesh;
                node.materialIndex = child->materialIndex;
                node.texTag = child->texTag;
                d.output = m;
                d.lastSource = child->mesh;
                d.lastChildMatrix = cm;
                d.lastKind = d.kind;
                d.lastAxis = d.axis;
                d.lastAmount = d.amount;
                ++count;
            }
        } else if (node.mesh) {
            node.mesh.reset();
            d.output.reset();
            d.lastSource.reset();
        }
    }
    for (const auto& c : node.children()) walk(*c, count);
}
} // namespace

int evaluateDeformers(SceneNode& root) {
    int n = 0;
    walk(root, n);
    return n;
}

} // namespace iron3d
