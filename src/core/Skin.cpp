#include "Skin.h"

#include "SceneNode.h"

namespace iron3d {
namespace {

SceneNode* byPath(SceneNode& any, const std::string& path) {
    if (path.empty()) return nullptr;
    SceneNode* cur = &any;
    while (cur->parent()) cur = cur->parent();
    size_t a = 0;
    while (cur && a <= path.size()) {
        const size_t b = path.find('/', a);
        const std::string part = path.substr(a, b == std::string::npos ? std::string::npos : b - a);
        SceneNode* next = nullptr;
        for (const auto& c : cur->children())
            if (c->name() == part) { next = c.get(); break; }
        cur = next;
        if (b == std::string::npos) break;
        a = b + 1;
    }
    return cur;
}

bool jointWorlds(SceneNode& node, const SkinData& s, std::vector<glm::mat4>& out) {
    out.clear();
    for (const std::string& p : s.jointPaths) {
        SceneNode* j = byPath(node, p);
        if (!j) return false;
        out.push_back(j->worldMatrix());
    }
    return true;
}

template <class F>
void walk(SceneNode& n, F&& fn) {
    fn(n);
    for (const auto& c : n.children()) walk(*c, fn);
}

} // namespace

int bindSkins(SceneNode& root) {
    int n = 0;
    walk(root, [&](SceneNode& node) {
        if (!node.skin || node.skin->bound) return;
        SkinData& s = *node.skin;
        std::vector<glm::mat4> jw;
        if (!s.bindMesh || s.influences.size() != s.bindMesh->vertices.size() || !jointWorlds(node, s, jw)) {
            node.skin.reset();
            return;
        }
        s.jointInvBind.resize(jw.size());
        for (size_t i = 0; i < jw.size(); ++i) s.jointInvBind[i] = glm::inverse(jw[i]);
        s.meshBindWorld = node.worldMatrix();
        s.lastJoint = jw;
        s.lastMeshWorld = s.meshBindWorld;
        s.output = node.mesh;
        s.bound = true;
        ++n;
    });
    return n;
}

int evaluateSkins(SceneNode& root) {
    int n = 0;
    walk(root, [&](SceneNode& node) {
        if (!node.skin || !node.skin->bound) return;
        SkinData& s = *node.skin;
        if (node.mesh != s.output) { node.skin.reset(); return; } // malla editada: Piel descartada
        std::vector<glm::mat4> jw;
        if (!jointWorlds(node, s, jw)) return;
        const glm::mat4 mw = node.worldMatrix();
        if (jw == s.lastJoint && mw == s.lastMeshWorld) return;

        std::vector<glm::mat4> m(jw.size());
        for (size_t i = 0; i < jw.size(); ++i) m[i] = jw[i] * s.jointInvBind[i];
        const glm::mat4 inv = glm::inverse(mw);
        const glm::mat3 invR(inv), bindR(s.meshBindWorld);

        auto out = std::make_shared<MeshData>(*s.bindMesh);
        for (size_t v = 0; v < out->vertices.size(); ++v) {
            const SkinInfluence& inf = s.influences[v];
            const glm::vec3 p0 = glm::vec3(s.meshBindWorld * glm::vec4(s.bindMesh->vertices[v].position, 1.0f));
            const glm::vec3 n0 = bindR * s.bindMesh->vertices[v].normal;
            float sum = 0.0f;
            glm::vec3 p(0.0f), nn(0.0f);
            for (int k = 0; k < 4; ++k) {
                if (inf.joint[k] < 0 || inf.weight[k] <= 0.0f) continue;
                const glm::mat4& mj = m[(size_t)inf.joint[k]];
                p += inf.weight[k] * glm::vec3(mj * glm::vec4(p0, 1.0f));
                nn += inf.weight[k] * (glm::mat3(mj) * n0);
                sum += inf.weight[k];
            }
            const float rest = 1.0f - sum;
            if (rest > 0.0f) { p += rest * p0; nn += rest * n0; }
            out->vertices[v].position = glm::vec3(inv * glm::vec4(p, 1.0f));
            const glm::vec3 nl = invR * nn;
            const float len = glm::length(nl);
            if (len > 1e-12f) out->vertices[v].normal = nl / len;
        }
        updateBounds(*out);
        s.lastJoint = std::move(jw);
        s.lastMeshWorld = mw;
        s.output = out;
        node.mesh = std::move(out);
        ++n;
    });
    return n;
}

} // namespace iron3d
