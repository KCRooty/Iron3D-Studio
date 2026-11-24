#include "Deform.h"
#include "DeformMore.h"
#include "Spline.h"
#include "MeshEdit.h"
#include "MeshTopology.h"
#include "Symmetry.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <sstream>

namespace iron3d {


MeshData subdivideCatmullClark(const MeshData& src, int levels) {
    MeshData cur = src;
    for (int lv = 0; lv < std::clamp(levels, 0, 4); ++lv) {
        const MeshTopology t = buildMeshTopology(cur);
        const size_t np = t.points.size(), nf = t.polys.size();
        size_t corners = 0;
        for (const auto& poly : t.polys) corners += poly.size();
        if (np == 0 || nf == 0 || corners > 400000) break;
        using Key = std::pair<int, int>;
        auto key = [](int a, int b) { return Key(std::min(a, b), std::max(a, b)); };

        std::vector<glm::vec3> pcol(np, glm::vec3(0.5f));
        std::vector<glm::vec2> puv(np, glm::vec2(0.0f));
        std::vector<char> seen(np, 0);
        for (size_t v = 0; v < cur.vertices.size(); ++v) {
            const int p = t.vertexPoint[v];
            if (!seen[(size_t)p]) { seen[(size_t)p] = 1; pcol[(size_t)p] = cur.vertices[v].color; puv[(size_t)p] = cur.vertices[v].uv; }
        }

        std::vector<glm::vec3> fp(nf, glm::vec3(0.0f));
        std::vector<std::vector<int>> pointFaces(np);
        std::map<Key, std::vector<int>> edgeFaces;
        for (size_t f = 0; f < nf; ++f) {
            const auto& c = t.polys[f];
            for (size_t i = 0; i < c.size(); ++i) {
                fp[f] += t.points[(size_t)c[i]];
                pointFaces[(size_t)c[i]].push_back((int)f);
                edgeFaces[key(c[i], c[(i + 1) % c.size()])].push_back((int)f);
            }
            fp[f] /= (float)c.size();
        }
        std::map<Key, int> edgeIndex;
        std::vector<glm::vec3> ep, emid;
        std::vector<Key> ends;
        std::vector<char> boundary;
        for (const auto& [k, fl] : edgeFaces) {
            edgeIndex[k] = (int)ep.size();
            const glm::vec3 mid = 0.5f * (t.points[(size_t)k.first] + t.points[(size_t)k.second]);
            emid.push_back(mid);
            ends.push_back(k);
            const bool inner = fl.size() == 2;
            boundary.push_back(inner ? 0 : 1);
            ep.push_back(inner ? 0.25f * (t.points[(size_t)k.first] + t.points[(size_t)k.second] + fp[(size_t)fl[0]] + fp[(size_t)fl[1]]) : mid);
        }
        std::vector<std::vector<int>> pointEdges(np);
        for (size_t e = 0; e < ends.size(); ++e) {
            pointEdges[(size_t)ends[e].first].push_back((int)e);
            pointEdges[(size_t)ends[e].second].push_back((int)e);
        }
        std::vector<glm::vec3> vp(np);
        for (size_t p = 0; p < np; ++p) {
            const glm::vec3& P = t.points[p];
            vp[p] = P;
            if (pointFaces[p].empty()) continue;
            std::vector<int> bnd;
            for (int e : pointEdges[p])
                if (boundary[(size_t)e]) bnd.push_back(e);
            if (!bnd.empty()) {
                if (bnd.size() == 2) {
                    glm::vec3 sum(0.0f);
                    for (int e : bnd) sum += t.points[(size_t)(ends[(size_t)e].first == (int)p ? ends[(size_t)e].second : ends[(size_t)e].first)];
                    vp[p] = (6.0f * P + sum) / 8.0f;
                }
                continue;
            }
            const float n = (float)pointFaces[p].size();
            glm::vec3 favg(0.0f), ravg(0.0f);
            for (int f : pointFaces[p]) favg += fp[(size_t)f];
            favg /= n;
            for (int e : pointEdges[p]) ravg += emid[(size_t)e];
            ravg /= (float)pointEdges[p].size();
            vp[p] = (favg + 2.0f * ravg + (n - 3.0f) * P) / n;
        }

        MeshData out;
        const size_t base = np, ebase = np + nf;
        out.vertices.resize(np + nf + ep.size());
        for (size_t p = 0; p < np; ++p) out.vertices[p] = {vp[p], pcol[p], glm::vec3(0.0f), puv[p]};
        for (size_t f = 0; f < nf; ++f) {
            glm::vec3 col(0.0f);
            glm::vec2 uv(0.0f);
            for (int q : t.polys[f]) { col += pcol[(size_t)q]; uv += puv[(size_t)q]; }
            const float k = 1.0f / (float)t.polys[f].size();
            out.vertices[base + f] = {fp[f], col * k, glm::vec3(0.0f), uv * k};
        }
        for (size_t e = 0; e < ep.size(); ++e)
            out.vertices[ebase + e] = {ep[e], 0.5f * (pcol[(size_t)ends[e].first] + pcol[(size_t)ends[e].second]), glm::vec3(0.0f),
                                       0.5f * (puv[(size_t)ends[e].first] + puv[(size_t)ends[e].second])};
        for (size_t f = 0; f < nf; ++f) {
            const auto& c = t.polys[f];
            for (size_t i = 0; i < c.size(); ++i) {
                const int cur0 = c[i], next = c[(i + 1) % c.size()], prev = c[(i + c.size() - 1) % c.size()];
                const uint32_t a = (uint32_t)cur0, b = (uint32_t)(ebase + (size_t)edgeIndex[key(cur0, next)]);
                const uint32_t m = (uint32_t)(base + f), d = (uint32_t)(ebase + (size_t)edgeIndex[key(prev, cur0)]);
                out.indices.insert(out.indices.end(), {a, b, m, a, m, d});
                out.faceTris.push_back(2);
                const glm::vec3 nrm = glm::cross(out.vertices[m].position - out.vertices[a].position,
                                                 out.vertices[d].position - out.vertices[b].position);
                for (uint32_t v : {a, b, m, d}) out.vertices[v].normal += nrm;
            }
        }
        for (Vertex& v : out.vertices)
            v.normal = glm::length(v.normal) > 1e-20f ? glm::normalize(v.normal) : glm::vec3(0.0f, 1.0f, 0.0f);
        updateBounds(out);
        cur = std::move(out);
    }
    return cur;
}

std::string nodePathFromRoot(const SceneNode& root, const SceneNode& node) {
    std::string path;
    for (const SceneNode* n = &node; n && n != &root; n = n->parent()) {
        const std::string seg = pathSegmentOf(*n);
        path = path.empty() ? seg : seg + "/" + path;
    }
    return path;
}

MeshData deformMeshData(const MeshData& src, int kind, int axis, float amount, const glm::mat4& toParent,
                        const DeformExtra* extra) {
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
    if (kind == 8) return subdivideCatmullClark(out, (int)std::lround(amount));
    if (kind == 12) {  // Bisel: todas las aristas; 100 % = la mitad de la arista mas corta
        const MeshTopology t = buildMeshTopology(out);
        float minEdge = 1e30f;
        for (const auto& e : t.edges) minEdge = std::min(minEdge, glm::length(t.points[(size_t)e.first] - t.points[(size_t)e.second]));
        if (minEdge > 1e29f || amount <= 0.0f) return out;
        return chamferMesh(out, std::clamp(amount / 100.0f, 0.0f, 1.0f) * 0.5f * minEdge);
    }
    if (kind == 9) {  // Explosion: cada poligono se aleja por su normal (100 % = medio tamano del objeto)
        const MeshTopology t = buildMeshTopology(out);
        glm::vec3 mn(1e30f), mx(-1e30f);
        for (const Vertex& v : out.vertices) { mn = glm::min(mn, v.position); mx = glm::max(mx, v.position); }
        const float shift = amount / 100.0f * 0.5f * glm::length(mx - mn);
        MeshData r;
        r.faceTris = out.faceTris;
        for (size_t tri = 0; tri * 3 + 2 < out.indices.size(); ++tri) {
            const int poly = tri < t.triPoly.size() ? t.triPoly[tri] : -1;
            const glm::vec3 off = poly >= 0 ? t.polyNormals[(size_t)poly] * shift : glm::vec3(0.0f);
            for (size_t k = 0; k < 3; ++k) {
                Vertex v = out.vertices[out.indices[tri * 3 + k]];
                v.position += off;
                r.indices.push_back((uint32_t)r.vertices.size());
                r.vertices.push_back(v);
            }
        }
        updateBounds(r);
        return r;
    }
    if (kind == 10) {  // Morph: mezcla con la malla del segundo hijo (mismo numero de vertices)
        if (extra && extra->second && extra->second->vertices.size() == out.vertices.size()) {
            const float k = std::clamp(amount / 100.0f, 0.0f, 1.0f);
            const glm::mat3 m2 = glm::mat3(extra->secondMatrix);
            for (size_t i = 0; i < out.vertices.size(); ++i) {
                Vertex& v = out.vertices[i];
                const Vertex& w = extra->second->vertices[i];
                v.position = glm::mix(v.position, glm::vec3(extra->secondMatrix * glm::vec4(w.position, 1.0f)), k);
                const glm::vec3 n = glm::mix(v.normal, m2 * w.normal, k);
                if (glm::length(n) > 1e-12f) v.normal = glm::normalize(n);
            }
        }
        updateBounds(out);
        return out;
    }
    if (kind == 11) {  // FFD: rejilla 2x2x2 sobre la caja del objeto, interpolacion trilineal
        if (extra && extra->ffd) {
            glm::vec3 mn(1e30f), mx(-1e30f);
            for (const Vertex& v : out.vertices) { mn = glm::min(mn, v.position); mx = glm::max(mx, v.position); }
            const glm::vec3 size = glm::max(mx - mn, glm::vec3(1e-6f));
            for (Vertex& v : out.vertices) {
                const glm::vec3 u = (v.position - mn) / size;
                glm::vec3 off(0.0f);
                for (int i = 0; i < 8; ++i) {
                    const float wx = (i & 1) ? u.x : 1.0f - u.x, wy = (i & 2) ? u.y : 1.0f - u.y, wz = (i & 4) ? u.z : 1.0f - u.z;
                    off += extra->ffd[i] * (wx * wy * wz);
                }
                v.position += off * size;
            }
        }
        updateBounds(out);
        return out;
    }
    glm::vec3 ctr(0.0f);
    float rad = 0.0f;
    if (kind == 6) {  // Esferizar: centro y radio medio de la malla
        for (const Vertex& v : out.vertices) ctr += v.position;
        ctr /= (float)out.vertices.size();
        for (const Vertex& v : out.vertices) rad += glm::length(v.position - ctr);
        rad /= (float)out.vertices.size();
    }
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
            case 5: {  // Aplastar y Estirar: estira el eje (100 % = el doble) y conserva el volumen
                const float st = std::max(0.05f, 1.0f + amount / 100.0f), r = 1.0f / std::sqrt(st);
                p[a] = mid + (p[a] - mid) * st;
                p[b] *= r;
                p[c] *= r;
                n[a] /= st;
                n[b] *= std::sqrt(st);
                n[c] *= std::sqrt(st);
                break;
            }
            case 6: {  // Esferizar: lleva los puntos hacia la esfera de radio medio (100 % = esfera)
                const float k = std::clamp(amount / 100.0f, -1.0f, 1.0f);
                const glm::vec3 d = p - ctr;
                const float dl = glm::length(d);
                if (dl > 1e-9f) {
                    const glm::vec3 dir = d / dl;
                    p = glm::mix(p, ctr + dir * rad, k);
                    n = glm::mix(n, dir, std::clamp(k, 0.0f, 1.0f));
                }
                break;
            }
            case 7: {  // Derretir: lo de arriba baja y se ensancha en la base (100 % = muy derretido)
                const float k = std::clamp(amount / 100.0f, 0.0f, 1.0f), tt = (p[a] - lo) / len;
                const float f = (1.0f + 0.6f * k * (1.0f - tt) * (1.0f - tt)) * (1.0f - 0.3f * k * tt);
                p[a] = lo + (p[a] - lo) * (1.0f - 0.6f * k * tt * tt);
                p[b] *= f;
                p[c] *= f;
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

SceneNode* secondMeshChild(SceneNode& node, const SceneNode* first) {
    for (const auto& c : node.children())
        if (c.get() != first && c->mesh && !c->symmetry.present && !c->deform.present) return c.get();
    return nullptr;
}

bool sameFfd(const glm::vec3* a, const glm::vec3* b) {
    for (int i = 0; i < 8; ++i)
        if (a[i] != b[i]) return false;
    return true;
}

float g_evalFrame = 0.0f, g_evalFps = 30.0f;

std::shared_ptr<const MeshData> meshInNodeSpace(const MeshData& m, const glm::mat4& cm) {
    MeshData out = m;
    const glm::mat3 nm = glm::mat3(cm);
    for (Vertex& v : out.vertices) {
        v.position = glm::vec3(cm * glm::vec4(v.position, 1.0f));
        v.normal = nm * v.normal;
        if (glm::length(v.normal) > 1e-12f) v.normal = glm::normalize(v.normal);
    }
    return std::make_shared<const MeshData>(std::move(out));
}

void walkMore(SceneNode& node, int& count) {
    DeformData& d = node.deform;
    SceneNode* child = firstMeshChild(node);
    if (!child && !deformKindNeedsNoChild(d.kind)) {
        if (node.mesh) { node.mesh.reset(); d.output.reset(); d.lastSource.reset(); }
        return;
    }
    const glm::mat4 cm = child ? child->localMatrix() : glm::mat4(1.0f);
    SceneNode* child2 = child ? secondMeshChild(node, child) : nullptr;
    const glm::mat4 cm2 = child2 ? child2->localMatrix() : glm::mat4(1.0f);
    const std::shared_ptr<const MeshData> mesh2 = child2 ? child2->mesh : nullptr;
    std::vector<std::shared_ptr<const MeshData>> allMeshes;
    std::vector<glm::mat4> allMats;
    if (deformKindUsesAllChildren(d.kind)) {
        for (const auto& c : node.children()) {
            allMats.push_back(c->localMatrix());
            allMeshes.push_back(c->mesh);
        }
    }
    std::vector<SplineData> splineDatas;
    std::vector<glm::mat4> splineMats;
    if (deformKindUsesSplines(d.kind)) {
        for (const auto& c : node.children())
            if (c->spline.present) {
                splineDatas.push_back(c->spline);
                splineMats.push_back(c->localMatrix());
            }
    }
    const bool timeDep = deformKindUsesTime(d.kind);
    glm::vec3 view(0.0f);
    if (d.kind == 33) view = glm::vec3(glm::inverse(node.worldMatrix()) * glm::vec4(lodViewPoint(), 1.0f));
    if (d.kind == 26 && mesh2 && (!d.bindRest || d.bindRest->vertices.size() != mesh2->vertices.size())) d.bindRest = mesh2;
    const bool same = d.lastSource == (child ? child->mesh : nullptr) && d.lastChildMatrix == cm && d.lastKind == d.kind &&
                      d.lastAxis == d.axis && d.lastAmount == d.amount && (deformKindOutputsSpline(d.kind) || node.mesh == d.output) &&
                      d.lastSource2 == mesh2 && d.lastChildMatrix2 == cm2 && sameFfd(d.lastFfd, d.ffd) &&
                      d.lastParam2 == d.param2 && d.lastFormula == d.formula && (!timeDep || d.lastFrame == g_evalFrame) &&
                      d.lastAllMeshes == allMeshes && d.lastAllMats == allMats && d.lastView == view &&
                      d.lastSplines == splineDatas && d.lastSplineMats == splineMats &&
                      (deformKindOutputsSpline(d.kind) || d.output);
    if (same) return;
    DeformMoreInput in;
    in.frame = g_evalFrame;
    in.fps = g_evalFps;
    in.param2 = d.param2;
    in.motion = child ? &child->animPos : nullptr;
    in.second = mesh2.get();
    in.secondMatrix = cm2;
    in.ffd = d.ffd;
    in.formula = &d.formula;
    in.rest = d.bindRest.get();
    for (size_t i = 0; i < allMeshes.size(); ++i)
        if (allMeshes[i]) in.all.push_back({allMeshes[i].get(), allMats[i]});
    if (d.kind == 32) {
        for (const auto& c : node.children()) {
            const glm::mat4 m = c->localMatrix();
            float r = 0.25f;
            if (c->mesh) r = 0.5f * glm::length(c->mesh->boundsMax - c->mesh->boundsMin) * 0.5f;
            const float s = std::max({glm::length(glm::vec3(m[0])), glm::length(glm::vec3(m[1])), glm::length(glm::vec3(m[2]))});
            in.spheres.push_back({glm::vec3(m[3]), std::max(r * s, 1e-3f)});
        }
    }
    in.viewPointNode = view;
    for (size_t i = 0; i < splineDatas.size(); ++i) {
        std::vector<SplineContour> local = splineContours(splineDatas[i]);
        std::vector<SplineContour> ns = local;
        for (auto& cc : ns) for (glm::vec3& p : cc.pts) p = glm::vec3(splineMats[i] * glm::vec4(p, 1.0f));
        in.splinesLocal.push_back(std::move(local));
        in.splines.push_back(std::move(ns));
    }
    if (d.kind == 46)
        for (const auto& c : node.children()) in.childMotions.push_back(&c->animPos);
    std::vector<SplineContour> splineOut;
    in.splineOut = &splineOut;
    MeshData src;
    if (child) src = *meshInNodeSpace(*child->mesh, cm);
    auto m = std::make_shared<const MeshData>(deformMore(std::move(src), d.kind, d.axis, d.amount, in));
    if (deformKindOutputsSpline(d.kind)) {
        SplineData out;
        out.present = true;
        out.generated = true;
        out.kind = 0;
        out.interp = 0;
        for (const auto& cc : splineOut) {
            out.extra.push_back(cc.pts);
            out.extraClosed.push_back(cc.closed);
        }
        node.spline = std::move(out);
        node.mesh.reset();
        node.primitive = PrimitiveType::None;
        d.output.reset();
    } else {
        if (node.spline.generated) node.spline = SplineData{};
        node.mesh = m;
        node.primitive = PrimitiveType::Mesh;
        d.output = m;
    }
    if (child) {
        node.materialIndex = child->materialIndex;
        node.texTag = child->texTag;
    }
    d.lastSource = child ? child->mesh : nullptr;
    d.lastChildMatrix = cm;
    d.lastKind = d.kind;
    d.lastAxis = d.axis;
    d.lastAmount = d.amount;
    d.lastSource2 = mesh2;
    d.lastChildMatrix2 = cm2;
    for (int i = 0; i < 8; ++i) d.lastFfd[i] = d.ffd[i];
    d.lastParam2 = d.param2;
    d.lastFormula = d.formula;
    d.lastFrame = g_evalFrame;
    d.lastAllMeshes = allMeshes;
    d.lastAllMats = allMats;
    d.lastView = view;
    d.lastSplines = splineDatas;
    d.lastSplineMats = splineMats;
    ++count;
}

bool kindIsDeformer(int k) { return k <= 27 || k == 49 || k == 50; }

void walk(SceneNode& node, int& count) {
    if (node.deform.present && !(kindIsDeformer(node.deform.kind) ? g_useDeformers : g_useGenerators)) {
        // apagado en el proyecto: pasa el primer hijo tal cual (Correccion)
        bakeParametricChildren(node);
        const int real = node.deform.kind;
        node.deform.kind = 16;
        walkMore(node, count);
        node.deform.kind = real;
    } else if (node.deform.present && node.deform.kind >= 13) {
        bakeParametricChildren(node);
        walkMore(node, count);
    } else if (node.deform.present) {
        DeformData& d = node.deform;
        bakeParametricChildren(node);
        if (SceneNode* child = firstMeshChild(node)) {
            const glm::mat4 cm = child->localMatrix();
            SceneNode* child2 = d.kind == 10 ? secondMeshChild(node, child) : nullptr;
            const glm::mat4 cm2 = child2 ? child2->localMatrix() : glm::mat4(1.0f);
            const std::shared_ptr<const MeshData> mesh2 = child2 ? child2->mesh : nullptr;
            const bool same = d.lastSource == child->mesh && d.lastChildMatrix == cm && d.lastKind == d.kind &&
                              d.lastAxis == d.axis && d.lastAmount == d.amount && node.mesh == d.output &&
                              d.lastSource2 == mesh2 && d.lastChildMatrix2 == cm2 && sameFfd(d.lastFfd, d.ffd);
            if (!same) {
                DeformExtra ex;
                ex.second = mesh2.get();
                ex.secondMatrix = cm2;
                ex.ffd = d.ffd;
                auto m = std::make_shared<const MeshData>(deformMeshData(*child->mesh, d.kind, d.axis, d.amount, cm, &ex));
                d.lastSource2 = mesh2;
                d.lastChildMatrix2 = cm2;
                for (int i = 0; i < 8; ++i) d.lastFfd[i] = d.ffd[i];
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

void setDeformTime(float frame, float fps) {
    g_evalFrame = frame;
    g_evalFps = fps;
}

int evaluateDeformers(SceneNode& root) {
    int n = 0;
    walk(root, n);
    return n;
}

namespace {
SceneNode* resolveNodePath(SceneNode& root, const std::string& path) {
    if (path.empty()) return nullptr;
    SceneNode* cur = &root;
    std::stringstream ss(path);
    std::string part;
    while (std::getline(ss, part, '/')) {
        SceneNode* next = childByPathSegment(*cur, part);
        if (!next) return nullptr;
        cur = next;
    }
    return cur == &root ? nullptr : cur;
}
} // namespace

namespace {
void walkInstances(SceneNode& root, SceneNode& node) {
    if (node.instance.present) {
        SceneNode* src = resolveNodePath(root, node.instance.source);
        if (src == &node) src = nullptr;
        if (src && src->mesh) {
            node.mesh = src->mesh;
            node.primitive = PrimitiveType::Mesh;
            node.materialIndex = src->materialIndex;
            node.texTag = src->texTag;
        } else if (src && (src->primitive == PrimitiveType::Cube || src->primitive == PrimitiveType::Sphere ||
                           src->primitive == PrimitiveType::Plane)) {
            node.mesh.reset();
            node.primitive = src->primitive;
            node.materialIndex = src->materialIndex;
            node.texTag = src->texTag;
        } else {
            node.mesh.reset();
            node.primitive = PrimitiveType::None;
        }
    }
    for (const auto& c : node.children()) walkInstances(root, *c);
}
} // namespace

namespace {
// Orienta el nodo (mira por -Z local, como SceneLighting) hacia el objeto `target`.
void aimAt(SceneNode& root, SceneNode& node, const std::string& target) {
    SceneNode* t = resolveNodePath(root, target);
    if (!t || t == &node) return;
    const glm::vec3 from = glm::vec3(node.worldMatrix()[3]), to = glm::vec3(t->worldMatrix()[3]);
    const glm::vec3 dirWorld = to - from;
    if (glm::length(dirWorld) < 1e-6f) return;
    const glm::mat3 pw = glm::mat3(node.parentWorldMatrix());
    if (std::fabs(glm::determinant(pw)) < 1e-12f) return;
    const glm::vec3 z = -glm::normalize(glm::inverse(pw) * dirWorld);  // el motor mira por -Z
    const glm::vec3 upRef = std::fabs(z.y) > 0.999f ? glm::vec3(0.0f, 0.0f, 1.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
    const glm::vec3 x = glm::normalize(glm::cross(upRef, z));
    const glm::vec3 y = glm::cross(z, x);
    node.rotationDeg = mat3ToHpb(glm::mat3(x, y, z));
}

void walkTargets(SceneNode& root, SceneNode& node) {
    if (node.primitive == PrimitiveType::Camera && !node.camera.target.empty()) aimAt(root, node, node.camera.target);
    if (node.primitive == PrimitiveType::Light && !node.light.target.empty()) aimAt(root, node, node.light.target);
    for (const auto& c : node.children()) walkTargets(root, *c);
}
} // namespace

void evaluateInstances(SceneNode& root) {
    walkInstances(root, root);
    walkTargets(root, root);
}

} // namespace iron3d
