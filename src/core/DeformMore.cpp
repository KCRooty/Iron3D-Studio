#include "DeformMore.h"

#include "Boolean.h"
#include "MeshEdit.h"
#include "MeshTopology.h"

#include <algorithm>
#include <functional>
#include <tuple>
#include <glm/gtc/matrix_transform.hpp>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <map>
#include <set>
#include <unordered_map>

namespace iron3d {

namespace {
constexpr float kCm = 200.0f;  // cm por unidad
constexpr float kPi = 3.14159265358979f;

glm::vec3 g_lodView{0.0f, 0.0f, 1e9f};

uint32_t hash32(uint32_t x) {
    x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16;
    return x;
}
float rnd(uint32_t seed, uint32_t i) { return (hash32(seed * 9781u + i * 6271u + 17u) & 0xffffff) / float(0xffffff); }

void boundsOf(const MeshData& m, glm::vec3& mn, glm::vec3& mx) {
    mn = glm::vec3(1e30f);
    mx = glm::vec3(-1e30f);
    for (const Vertex& v : m.vertices) { mn = glm::min(mn, v.position); mx = glm::max(mx, v.position); }
    if (m.vertices.empty()) mn = mx = glm::vec3(0.0f);
}

void recomputeNormals(MeshData& m) {
    for (Vertex& v : m.vertices) v.normal = glm::vec3(0.0f);
    for (size_t t = 0; t + 2 < m.indices.size(); t += 3) {
        Vertex &a = m.vertices[m.indices[t]], &b = m.vertices[m.indices[t + 1]], &c = m.vertices[m.indices[t + 2]];
        const glm::vec3 n = glm::cross(b.position - a.position, c.position - a.position);
        a.normal += n; b.normal += n; c.normal += n;
    }
    for (Vertex& v : m.vertices) v.normal = glm::length(v.normal) > 1e-20f ? glm::normalize(v.normal) : glm::vec3(0, 1, 0);
}

// Normales por punto (media de las de sus vertices) para que los desplazamientos no abran costuras.
void smoothNormalsByPoint(MeshData& m) {
    std::map<std::tuple<float, float, float>, glm::vec3> acc;
    for (const Vertex& v : m.vertices) acc[{v.position.x, v.position.y, v.position.z}] += v.normal;
    for (Vertex& v : m.vertices) {
        const glm::vec3 n = acc[{v.position.x, v.position.y, v.position.z}];
        if (glm::length(n) > 1e-20f) v.normal = glm::normalize(n);
    }
}

// Separa cada poligono en sus propios vertices; vertPoly[v] = poligono (o -1).
MeshData unweld(const MeshData& src, const MeshTopology& t, std::vector<int>& vertPoly) {
    MeshData r;
    r.faceTris = src.faceTris;
    vertPoly.clear();
    for (size_t tri = 0; tri * 3 + 2 < src.indices.size(); ++tri) {
        const int poly = tri < t.triPoly.size() ? t.triPoly[tri] : -1;
        for (size_t k = 0; k < 3; ++k) {
            r.indices.push_back((uint32_t)r.vertices.size());
            r.vertices.push_back(src.vertices[src.indices[tri * 3 + k]]);
            vertPoly.push_back(poly);
        }
    }
    return r;
}

glm::mat3 axisAngle(const glm::vec3& axis, float ang) {
    const glm::vec3 a = glm::normalize(axis);
    const float c = std::cos(ang), s = std::sin(ang), t = 1.0f - c;
    return glm::mat3(t * a.x * a.x + c, t * a.x * a.y + s * a.z, t * a.x * a.z - s * a.y,
                     t * a.x * a.y - s * a.z, t * a.y * a.y + c, t * a.y * a.z + s * a.x,
                     t * a.x * a.z + s * a.y, t * a.y * a.z - s * a.x, t * a.z * a.z + c);
}

glm::vec3 closestOnTri(const glm::vec3& p, const glm::vec3& a, const glm::vec3& b, const glm::vec3& c) {
    const glm::vec3 ab = b - a, ac = c - a, ap = p - a;
    const float d1 = glm::dot(ab, ap), d2 = glm::dot(ac, ap);
    if (d1 <= 0 && d2 <= 0) return a;
    const glm::vec3 bp = p - b;
    const float d3 = glm::dot(ab, bp), d4 = glm::dot(ac, bp);
    if (d3 >= 0 && d4 <= d3) return b;
    const float vc = d1 * d4 - d3 * d2;
    if (vc <= 0 && d1 >= 0 && d3 <= 0) return a + ab * (d1 / (d1 - d3));
    const glm::vec3 cp = p - c;
    const float d5 = glm::dot(ab, cp), d6 = glm::dot(ac, cp);
    if (d6 >= 0 && d5 <= d6) return c;
    const float vb = d5 * d2 - d1 * d6;
    if (vb <= 0 && d2 >= 0 && d6 <= 0) return a + ac * (d2 / (d2 - d6));
    const float va = d3 * d6 - d5 * d4;
    if (va <= 0 && (d4 - d3) >= 0 && (d5 - d6) >= 0) return b + (c - b) * ((d4 - d3) / ((d4 - d3) + (d5 - d6)));
    const float denom = 1.0f / (va + vb + vc);
    return a + ab * (vb * denom) + ac * (vc * denom);
}

struct TriSoup {
    std::vector<glm::vec3> p;  // 3 por triangulo
    std::vector<glm::vec3> n;  // 1 por triangulo
};
TriSoup soupOf(const MeshData& m, const glm::mat4& xf) {
    TriSoup s;
    for (size_t t = 0; t + 2 < m.indices.size(); t += 3) {
        glm::vec3 q[3];
        for (int k = 0; k < 3; ++k) q[k] = glm::vec3(xf * glm::vec4(m.vertices[m.indices[t + k]].position, 1.0f));
        const glm::vec3 n = glm::cross(q[1] - q[0], q[2] - q[0]);
        if (glm::length(n) < 1e-20f) continue;
        s.p.insert(s.p.end(), {q[0], q[1], q[2]});
        s.n.push_back(glm::normalize(n));
    }
    return s;
}
bool closestOnSoup(const TriSoup& s, const glm::vec3& p, glm::vec3& best, glm::vec3& bestN) {
    float bd = 1e30f;
    bool any = false;
    for (size_t t = 0; t < s.n.size(); ++t) {
        const glm::vec3 q = closestOnTri(p, s.p[t * 3], s.p[t * 3 + 1], s.p[t * 3 + 2]);
        const float d = glm::dot(q - p, q - p);
        if (d < bd) { bd = d; best = q; bestN = s.n[t]; any = true; }
    }
    return any;
}
// Rayo contra la sopa (Moller-Trumbore); t mas cercano en cualquier sentido de `dir` si twoSided.
bool raySoup(const TriSoup& s, const glm::vec3& o, const glm::vec3& dir, float& tBest) {
    bool hit = false;
    tBest = 1e30f;
    for (size_t t = 0; t < s.n.size(); ++t) {
        const glm::vec3 &a = s.p[t * 3], &b = s.p[t * 3 + 1], &c = s.p[t * 3 + 2];
        const glm::vec3 e1 = b - a, e2 = c - a, pv = glm::cross(dir, e2);
        const float det = glm::dot(e1, pv);
        if (std::fabs(det) < 1e-12f) continue;
        const float inv = 1.0f / det;
        const glm::vec3 tv = o - a;
        const float u = glm::dot(tv, pv) * inv;
        if (u < 0 || u > 1) continue;
        const glm::vec3 qv = glm::cross(tv, e1);
        const float v = glm::dot(dir, qv) * inv;
        if (v < 0 || u + v > 1) continue;
        const float tt = glm::dot(e2, qv) * inv;
        if (std::fabs(tt) < std::fabs(tBest)) { tBest = tt; hit = true; }
    }
    return hit;
}

float valueNoise(const glm::vec3& p) {
    const glm::vec3 i = glm::floor(p), f = p - i;
    const glm::vec3 u = f * f * (3.0f - 2.0f * f);
    auto h = [&](int x, int y, int z) {
        return rnd(1u, (uint32_t)((int)i.x + x) * 73856093u ^ (uint32_t)((int)i.y + y) * 19349663u ^ (uint32_t)((int)i.z + z) * 83492791u);
    };
    const float a = glm::mix(h(0, 0, 0), h(1, 0, 0), u.x), b = glm::mix(h(0, 1, 0), h(1, 1, 0), u.x);
    const float c = glm::mix(h(0, 0, 1), h(1, 0, 1), u.x), d = glm::mix(h(0, 1, 1), h(1, 1, 1), u.x);
    return glm::mix(glm::mix(a, b, u.y), glm::mix(c, d, u.y), u.z) * 2.0f - 1.0f;
}
float fbm(glm::vec3 p) {
    float s = 0.0f, amp = 0.5f;
    for (int o = 0; o < 4; ++o) { s += amp * valueNoise(p); p *= 2.03f; amp *= 0.5f; }
    return s;
}

// --- Formula: descenso recursivo -------------------------------------------------------------
struct Parser {
    const std::string& s;
    size_t i = 0;
    float x, y, z, u, v, t;
    std::string err;
    void ws() { while (i < s.size() && std::isspace((unsigned char)s[i])) ++i; }
    bool eat(char c) { ws(); if (i < s.size() && s[i] == c) { ++i; return true; } return false; }
    float expr() {
        float a = term();
        for (;;) {
            if (eat('+')) a += term();
            else if (eat('-')) a -= term();
            else return a;
        }
    }
    float term() {
        float a = power();
        for (;;) {
            if (eat('*')) a *= power();
            else if (eat('/')) { const float b = power(); a = std::fabs(b) < 1e-20f ? 0.0f : a / b; }
            else return a;
        }
    }
    float power() {
        float a = unary();
        if (eat('^')) a = std::pow(a, power());
        return a;
    }
    float unary() {
        if (eat('-')) return -unary();
        if (eat('+')) return unary();
        return atom();
    }
    float atom() {
        ws();
        if (eat('(')) { const float a = expr(); if (!eat(')')) err = "falta )"; return a; }
        if (i < s.size() && (std::isdigit((unsigned char)s[i]) || s[i] == '.')) {
            size_t j = i;
            while (j < s.size() && (std::isdigit((unsigned char)s[j]) || s[j] == '.')) ++j;
            const float val = std::strtof(s.substr(i, j - i).c_str(), nullptr);
            i = j;
            return val;
        }
        size_t j = i;
        while (j < s.size() && std::isalpha((unsigned char)s[j])) ++j;
        std::string id = s.substr(i, j - i);
        for (char& c : id) c = (char)std::tolower((unsigned char)c);
        i = j;
        if (id.empty()) { err = "simbolo inesperado"; ++i; return 0.0f; }
        if (id == "x") return x;
        if (id == "y") return y;
        if (id == "z") return z;
        if (id == "u") return u;
        if (id == "v") return v;
        if (id == "t") return t;
        if (id == "pi") return kPi;
        if (!eat('(')) { err = "variable desconocida: " + id; return 0.0f; }
        const float a = expr();
        if (!eat(')')) err = "falta )";
        if (id == "sin") return std::sin(a);
        if (id == "cos") return std::cos(a);
        if (id == "tan") return std::tan(a);
        if (id == "sqrt") return std::sqrt(std::max(a, 0.0f));
        if (id == "abs") return std::fabs(a);
        if (id == "exp") return std::exp(a);
        if (id == "log") return a > 0 ? std::log(a) : 0.0f;
        if (id == "floor") return std::floor(a);
        if (id == "noise") return valueNoise(glm::vec3(a, 0.37f, 0.71f));
        err = "funcion desconocida: " + id;
        return 0.0f;
    }
};

// --- Metaball: tetraedros en marcha ------------------------------------------------------------
void marchTets(const std::vector<std::pair<glm::vec3, float>>& balls, const glm::vec3& mn, const glm::vec3& mx, float cell,
               MeshData& out) {
    auto field = [&](const glm::vec3& p) {
        float f = 0.0f;
        for (const auto& b : balls) {
            const float r2 = b.second * b.second, d2 = glm::dot(p - b.first, p - b.first);
            if (d2 < r2 * 4.0f) { const float q = 1.0f - d2 / (r2 * 4.0f); f += q * q * q; }
        }
        return f - 0.3f;  // umbral
    };
    const glm::ivec3 n = glm::max(glm::ivec3(glm::ceil((mx - mn) / cell)), glm::ivec3(1));
    if ((long long)n.x * n.y * n.z > 2'000'000) return;
    std::vector<float> val((size_t)(n.x + 1) * (n.y + 1) * (n.z + 1));
    auto idx = [&](int i, int j, int k) { return ((size_t)k * (n.y + 1) + j) * (n.x + 1) + i; };
    for (int k = 0; k <= n.z; ++k)
        for (int j = 0; j <= n.y; ++j)
            for (int i = 0; i <= n.x; ++i) val[idx(i, j, k)] = field(mn + glm::vec3(i, j, k) * cell);
    static const int tets[6][4] = {{0, 5, 1, 6}, {0, 1, 2, 6}, {0, 2, 3, 6}, {0, 3, 7, 6}, {0, 7, 4, 6}, {0, 4, 5, 6}};
    static const int co[8][3] = {{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}, {0, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 1, 1}};
    auto emit = [&](const glm::vec3& a, const glm::vec3& b, const glm::vec3& c) {
        const uint32_t base = (uint32_t)out.vertices.size();
        for (const glm::vec3& p : {a, b, c}) out.vertices.push_back({p, glm::vec3(0.8f), glm::vec3(0, 1, 0), glm::vec2(0.0f)});
        out.indices.insert(out.indices.end(), {base, base + 1, base + 2});
    };
    for (int k = 0; k < n.z; ++k)
        for (int j = 0; j < n.y; ++j)
            for (int i = 0; i < n.x; ++i) {
                glm::vec3 p[8];
                float f[8];
                for (int c = 0; c < 8; ++c) {
                    p[c] = mn + glm::vec3(i + co[c][0], j + co[c][1], k + co[c][2]) * cell;
                    f[c] = val[idx(i + co[c][0], j + co[c][1], k + co[c][2])];
                }
                for (const auto& tt : tets) {
                    int in[4], out_[4], ni = 0, no = 0;
                    for (int q = 0; q < 4; ++q) (f[tt[q]] > 0 ? in[ni++] : out_[no++]) = tt[q];
                    if (ni == 0 || ni == 4) continue;
                    auto lerp = [&](int a, int b) { const float tq = f[a] / (f[a] - f[b]); return p[a] + (p[b] - p[a]) * tq; };
                    // orientacion: normal hacia fuera (de dentro a fuera)
                    auto oriented = [&](glm::vec3 a, glm::vec3 b, glm::vec3 c, const glm::vec3& inside) {
                        if (glm::dot(glm::cross(b - a, c - a), a - inside) < 0) std::swap(b, c);
                        emit(a, b, c);
                    };
                    if (ni == 1) {
                        oriented(lerp(in[0], out_[0]), lerp(in[0], out_[1]), lerp(in[0], out_[2]), p[in[0]]);
                    } else if (ni == 3) {
                        const glm::vec3 ctr = (p[in[0]] + p[in[1]] + p[in[2]]) / 3.0f;
                        oriented(lerp(in[0], out_[0]), lerp(in[1], out_[0]), lerp(in[2], out_[0]), ctr);
                    } else {
                        const glm::vec3 a = lerp(in[0], out_[0]), b = lerp(in[0], out_[1]), c = lerp(in[1], out_[1]), d = lerp(in[1], out_[0]);
                        const glm::vec3 ctr = (p[in[0]] + p[in[1]]) * 0.5f;
                        oriented(a, b, c, ctr);
                        oriented(a, c, d, ctr);
                    }
                }
            }
}

// Une vertices con la misma posicion (para que las normales salgan suaves en mallas sopa).
void weldExact(MeshData& m, float tol) {
    std::unordered_map<long long, uint32_t> map;
    std::vector<Vertex> verts;
    std::vector<uint32_t> remap(m.vertices.size());
    const float inv = 1.0f / std::max(tol, 1e-9f);
    for (size_t i = 0; i < m.vertices.size(); ++i) {
        const glm::vec3 q = glm::round(m.vertices[i].position * inv);
        const long long key = ((long long)q.x * 73856093LL) ^ ((long long)q.y * 19349663LL) ^ ((long long)q.z * 83492791LL);
        auto it = map.find(key);
        if (it != map.end() && glm::length(verts[it->second].position - m.vertices[i].position) <= tol * 1.5f) {
            remap[i] = it->second;
        } else {
            remap[i] = (uint32_t)verts.size();
            map[key] = remap[i];
            verts.push_back(m.vertices[i]);
        }
    }
    for (uint32_t& ix : m.indices) ix = remap[ix];
    m.vertices = std::move(verts);
    // quita triangulos degenerados
    std::vector<uint32_t> idx;
    for (size_t t = 0; t + 2 < m.indices.size(); t += 3) {
        const uint32_t a = m.indices[t], b = m.indices[t + 1], c = m.indices[t + 2];
        if (a != b && b != c && a != c) idx.insert(idx.end(), {a, b, c});
    }
    if (idx.size() != m.indices.size()) m.faceTris.clear();
    m.indices = std::move(idx);
}

void appendMesh(MeshData& dst, const MeshData& src, const glm::mat4& xf) {
    const uint32_t base = (uint32_t)dst.vertices.size();
    const glm::mat3 nm = glm::mat3(xf);
    for (Vertex v : src.vertices) {
        v.position = glm::vec3(xf * glm::vec4(v.position, 1.0f));
        v.normal = nm * v.normal;
        if (glm::length(v.normal) > 1e-12f) v.normal = glm::normalize(v.normal);
        dst.vertices.push_back(v);
    }
    for (uint32_t i : src.indices) dst.indices.push_back(base + i);
    const std::vector<uint32_t> ft = faceTriCounts(src);
    dst.faceTris.insert(dst.faceTris.end(), ft.begin(), ft.end());
}

glm::mat4 rotAxisMat(int axis, float ang) {
    glm::vec3 ax(0.0f);
    ax[axis] = 1.0f;
    return glm::rotate(glm::mat4(1.0f), ang, ax);
}

// Componentes conexas por puntos; comp[poly].
std::vector<int> polyComponents(const MeshTopology& t, int& count) {
    std::vector<int> parent(t.points.size());
    for (size_t i = 0; i < parent.size(); ++i) parent[i] = (int)i;
    std::function<int(int)> find = [&](int a) { return parent[a] == a ? a : parent[a] = find(parent[a]); };
    for (const auto& p : t.polys)
        for (size_t k = 1; k < p.size(); ++k) parent[find(p[k])] = find(p[0]);
    std::map<int, int> ids;
    std::vector<int> comp(t.polys.size(), 0);
    for (size_t f = 0; f < t.polys.size(); ++f) {
        if (t.polys[f].empty()) continue;
        const int r = find(t.polys[f][0]);
        auto it = ids.find(r);
        if (it == ids.end()) it = ids.emplace(r, (int)ids.size()).first;
        comp[f] = it->second;
    }
    count = (int)ids.size();
    return comp;
}
} // namespace

void setLodViewPoint(const glm::vec3& world) { g_lodView = world; }
const glm::vec3& lodViewPoint() { return g_lodView; }

bool evalFormula(const std::string& expr, float x, float y, float z, float u, float v, float t, float& out, std::string* err) {
    Parser p{expr};
    p.x = x; p.y = y; p.z = z; p.u = u; p.v = v; p.t = t;
    out = p.expr();
    p.ws();
    if (p.err.empty() && p.i < expr.size()) p.err = "sobra texto al final";
    if (!std::isfinite(out)) out = 0.0f;
    if (!p.err.empty()) {
        if (err) *err = p.err;
        out = 0.0f;
        return false;
    }
    return true;
}

const char* deformKindName(int kind) {
    static const char* names[kDeformKindCount] = {
        "Doblar", "Enroscar", "Afilar", "Hinchar", "Sesgar", "Aplastar y Estirar", "Esferizar", "Derretir",
        "Subdivisión Superficie", "Explosión", "Morph", "FFD", "Bisel", "Jiggle", "FX Explosión", "Romper", "Corrección",
        "Cubrir", "Superficie", "Envoltura", "Cámara", "Colisión", "Desplazador", "Fórmula", "Viento", "Suavizado", "Malla",
        "Spline", "Booleano", "Conector", "Reducción de Polígonos", "Formación de Átomos", "Metaball", "LOD", "Matriz",
        "Matriz MoGraph", "Fractura", "Fractura Voronoi", "MoInstance", "MoExtrude", "PolyFX", "Extrusión", "Torno", "Forro",
        "Recorrido", "MoText", "Trazador", "MoSpline", "Máscara de Spline", "Rail de Splines", "Envolver Spline", "Bezier"};
    return kind >= 0 && kind < kDeformKindCount ? names[kind] : "?";
}
bool deformKindUsesTime(int kind) { return kind == 13 || kind == 23 || kind == 24 || kind == 38 || kind == 46; }
bool deformKindOutputsSpline(int kind) { return kind == 46 || kind == 47 || kind == 48; }
bool deformKindUsesSplines(int kind) { return kind == 27 || (kind >= 41 && kind <= 50 && kind != 45 && kind != 46); }
bool deformKindUsesAllChildren(int kind) { return kind == 29 || kind == 32 || kind == 33; }
bool deformKindNeedsNoChild(int kind) { return kind == 35 || kind == 32 || (kind >= 41 && kind <= 48) || kind == 51; }

const char* deformAmountLabel(int kind) {
    switch (kind) {
        case 13: return "Fuerza";
        case 14: return "Tiempo";
        case 15: return "Tiempo";
        case 16: return nullptr;
        case 17: case 18: case 21: return "Fuerza";
        case 19: return "Envolver";
        case 20: return nullptr;
        case 22: return "Altura";
        case 23: return "Fuerza";
        case 24: return "Amplitud";
        case 25: return "Fuerza";
        case 26: return "Fuerza";
        case 28: return nullptr;
        case 29: return "Tolerancia";
        case 30: return "Reducir al";
        case 31: return "Radio de Cilindro";
        case 32: return nullptr;
        case 33: return "Distancia por Nivel";
        case 34: return "Radio";
        case 35: return "Separación";
        case 36: return "Distancia";
        case 37: return "Separación";
        case 38: return "Paso (fotogramas)";
        case 39: return "Distancia";
        case 40: return "Intensidad";
        case 27: return "Radio";
        case 41: return "Distancia";
        case 42: return "Ángulo";
        case 43: return nullptr;
        case 44: return "Escala Final";
        case 45: return "Altura";
        case 46: return nullptr;
        case 47: return "Crecimiento";
        case 48: return nullptr;
        case 51: return nullptr;
        case 49: return "Fuerza";
        case 50: return "Fuerza";
        default: return "Fuerza";
    }
}
const char* deformParam2Label(int kind) {
    switch (kind) {
        case 13: return "Rigidez";
        case 22: return "Tamaño del Ruido";
        case 24: return "Frecuencia";
        case 25: return "Iteraciones";
        case 28: return "Modo";
        case 31: return "Radio de Esfera";
        case 32: return "Resolución";
        case 34: return "Copias";
        case 35: return "Cubos por Lado";
        case 37: return "Celdas";
        case 38: return "Copias";
        case 39: return "Pasos";
        case 41: return "Segmentos";
        case 42: return "Segmentos";
        case 43: return "Subdivisiones";
        case 44: return "Giro";
        case 45: return "Profundidad";
        case 46: return "Longitud (fotogramas)";
        case 47: return "Segmentos";
        case 48: return "Modo";
        case 51: return "Subdivisiones";
        default: return nullptr;
    }
}
float deformDefaultAmount(int kind) {
    switch (kind) {
        case 13: return 100.0f;
        case 14: return 30.0f;
        case 15: return 50.0f;
        case 19: return 100.0f;
        case 22: return 10.0f;
        case 24: return 30.0f;
        case 29: return 0.5f;
        case 30: return 50.0f;
        case 31: return 2.0f;
        case 33: return 1000.0f;
        case 34: return 200.0f;
        case 35: return 50.0f;
        case 36: return 30.0f;
        case 37: return 5.0f;
        case 38: return 3.0f;
        case 39: return 10.0f;
        case 40: return 30.0f;
        case 27: return 100.0f;
        case 41: return 40.0f;
        case 42: return 360.0f;
        case 44: return 100.0f;
        case 45: return 100.0f;
        case 47: return 100.0f;
        default: return 100.0f;
    }
}
float deformDefaultParam2(int kind) {
    switch (kind) {
        case 13: return 2.0f;
        case 22: return 50.0f;
        case 24: return 1.0f;
        case 25: return 5.0f;
        case 28: return 1.0f;
        case 31: return 4.0f;
        case 32: return 10.0f;
        case 34: return 6.0f;
        case 35: return 3.0f;
        case 37: return 12.0f;
        case 38: return 5.0f;
        case 39: return 1.0f;
        case 41: return 1.0f;
        case 42: return 24.0f;
        case 43: return 4.0f;
        case 44: return 0.0f;
        case 45: return 20.0f;
        case 46: return 30.0f;
        case 47: return 20.0f;
        case 48: return 0.0f;
        case 51: return 12.0f;
        default: return 1.0f;
    }
}

MeshData deformMore(MeshData out, int kind, int axis, float amount, const DeformMoreInput& in) {
    const int a = std::clamp(axis, 0, 2), b = (a + 1) % 3, c = (a + 2) % 3;
    glm::vec3 mn, mx;
    boundsOf(out, mn, mx);
    const glm::vec3 size = glm::max(mx - mn, glm::vec3(1e-6f));
    const glm::vec3 ctr = (mn + mx) * 0.5f;
    const float t = in.frame / std::max(in.fps, 1.0f);
    const float k01 = amount / 100.0f;

    switch (kind) {
        case 13: {  // Jiggle
            if (!in.motion || in.motion->empty()) break;
            const float stiff = std::max(in.param2, 0.05f);   // Hz
            const float w0 = 2.0f * kPi * stiff, damp = 0.12f;
            const int f0 = in.motion->keys.front().frame;
            const int steps = 4;
            glm::vec3 x = in.motion->evaluate((float)f0), vel(0.0f);
            const float dt = 1.0f / (std::max(in.fps, 1.0f) * steps);
            for (float f = (float)f0; f < in.frame; f += 1.0f / steps) {
                const glm::vec3 target = in.motion->evaluate(std::min(f + 1.0f / steps, in.frame));
                const glm::vec3 acc = -w0 * w0 * (x - target) - 2.0f * damp * w0 * vel;
                vel += acc * dt;
                x += vel * dt;
            }
            const glm::vec3 lag = glm::mat3(in.parentToNode) * (x - in.motion->evaluate(in.frame)) * k01;
            for (Vertex& v : out.vertices) {
                const float wgt = std::clamp((v.position[a] - mn[a]) / size[a], 0.0f, 1.0f);
                v.position += lag * wgt * wgt;
            }
            break;
        }
        case 14:    // FX Explosion
        case 15:    // Romper
        case 40: {  // PolyFX
            MeshTopology topo = buildMeshTopology(out);
            std::vector<int> vp;
            MeshData r = unweld(out, topo, vp);
            const float tm = std::max(0.0f, k01);
            const float diag = glm::length(size);
            for (size_t i = 0; i < r.vertices.size(); ++i) {
                const int poly = vp[i];
                if (poly < 0) continue;
                const glm::vec3 pc = polyCenter(topo, poly);
                const glm::vec3 rax = glm::normalize(glm::vec3(rnd(3, poly) - 0.5f, rnd(4, poly) - 0.5f, rnd(5, poly) - 0.5f) + 1e-4f);
                Vertex& v = r.vertices[i];
                glm::vec3 local = v.position - pc;
                glm::vec3 newC = pc;
                float ang = 0.0f;
                if (kind == 14) {
                    glm::vec3 dir = pc - ctr;
                    dir = glm::length(dir) > 1e-9f ? glm::normalize(dir) : topo.polyNormals[(size_t)poly];
                    newC = pc + dir * tm * diag * (0.6f + 0.8f * rnd(6, poly));
                    newC[1] -= 0.5f * 2.0f * tm * tm * diag;  // gravedad
                    ang = tm * 2.0f * kPi * (0.5f + rnd(7, poly));
                } else if (kind == 15) {
                    const float delay = 0.6f * rnd(8, poly);
                    const float tt = std::max(0.0f, tm - delay) / (1.0f - delay + 1e-6f);
                    const float drop = std::min(pc[a] - mn[a], 0.5f * 9.0f * tt * tt * size[a]);
                    newC[a] = pc[a] - drop;
                    ang = tt * 0.5f * kPi * rnd(9, poly);
                } else {
                    newC = pc + (glm::vec3(rnd(10, poly), rnd(11, poly), rnd(12, poly)) - 0.5f) * tm * diag * 0.5f;
                    ang = tm * kPi * (rnd(13, poly) - 0.5f);
                    local *= std::max(0.05f, 1.0f - tm * rnd(14, poly));
                }
                const glm::mat3 R = axisAngle(rax, ang);
                v.position = newC + R * local;
                v.normal = R * v.normal;
            }
            updateBounds(r);
            return r;
        }
        case 16: break;  // Correccion
        case 17: case 21: {  // Cubrir, Colision
            if (!in.second) break;
            const TriSoup s = soupOf(*in.second, in.secondMatrix);
            if (s.n.empty()) break;
            if ((double)s.n.size() * out.vertices.size() > 4e8) break;  // demasiado grande para fuerza bruta
            for (Vertex& v : out.vertices) {
                glm::vec3 q, qn;
                if (!closestOnSoup(s, v.position, q, qn)) continue;
                if (kind == 17) v.position = glm::mix(v.position, q, std::clamp(k01, 0.0f, 1.0f));
                else if (glm::dot(v.position - q, qn) < 0.0f) v.position = glm::mix(v.position, q + qn * 1e-4f, std::clamp(k01, 0.0f, 1.0f));
            }
            break;
        }
        case 18: {  // Superficie
            if (!in.second) break;
            const TriSoup s = soupOf(*in.second, in.secondMatrix);
            if (s.n.empty() || (double)s.n.size() * out.vertices.size() > 4e8) break;
            glm::vec3 dir(0.0f);
            dir[a] = -1.0f;
            for (Vertex& v : out.vertices) {
                const float off = v.position[a] - mn[a];
                glm::vec3 base = v.position;
                base[a] = mn[a];
                float th;
                if (!raySoup(s, base, dir, th)) continue;
                glm::vec3 np = v.position;
                np[a] = base[a] - th + off;
                v.position = glm::mix(v.position, np, std::clamp(k01, 0.0f, 1.0f));
            }
            break;
        }
        case 19: {  // Envoltura: enrolla el eje b alrededor del eje a
            const float theta = 2.0f * kPi * k01;
            if (std::fabs(theta) < 1e-5f) break;
            const float R = size[b] / theta;
            for (Vertex& v : out.vertices) {
                const float ang = (v.position[b] - ctr[b]) / size[b] * theta;
                const float rr = R - (v.position[c] - ctr[c]);
                const float nb = ctr[b] + rr * std::sin(ang), nc = ctr[c] + R - rr * std::cos(ang);
                v.position[b] = nb;
                v.position[c] = nc;
            }
            recomputeNormals(out);
            break;
        }
        case 20: {  // Camara: rejilla 2D sobre (b, c)
            if (!in.ffd) break;
            for (Vertex& v : out.vertices) {
                const float ub = (v.position[b] - mn[b]) / size[b], uc = (v.position[c] - mn[c]) / size[c];
                glm::vec3 off(0.0f);
                for (int i = 0; i < 4; ++i) {
                    const float w = ((i & 1) ? ub : 1 - ub) * ((i & 2) ? uc : 1 - uc);
                    off[b] += w * in.ffd[i].x * size[b];
                    off[c] += w * in.ffd[i].y * size[c];
                }
                v.position += off;
            }
            recomputeNormals(out);
            break;
        }
        case 22: {  // Desplazador
            smoothNormalsByPoint(out);
            const float h = amount / kCm, sc = std::max(in.param2, 0.1f) / kCm;
            for (Vertex& v : out.vertices) v.position += v.normal * (fbm(v.position / sc) * h);
            recomputeNormals(out);
            break;
        }
        case 23: {  // Formula
            if (!in.formula) break;
            smoothNormalsByPoint(out);
            const float half = 0.5f * glm::length(size);
            for (Vertex& v : out.vertices) {
                const glm::vec3 q = (v.position - ctr) / (0.5f * size);
                float d = 0.0f;
                evalFormula(*in.formula, q.x, q.y, q.z, (v.position[b] - mn[b]) / size[b], (v.position[c] - mn[c]) / size[c], t, d);
                v.position += v.normal * d * half * k01;
            }
            recomputeNormals(out);
            break;
        }
        case 24: {  // Viento
            const float amp = k01 * 0.1f * glm::length(size), freq = std::max(in.param2, 0.01f);
            for (Vertex& v : out.vertices) {
                const float ub = (v.position[b] - mn[b]) / size[b];
                const float ua = std::clamp((v.position[a] - mn[a]) / size[a], 0.0f, 1.0f);
                v.position[c] += amp * ua * std::sin(2.0f * kPi * (freq * ub - t));
            }
            recomputeNormals(out);
            break;
        }
        case 25: {  // Suavizado
            MeshTopology topo = buildMeshTopology(out);
            std::vector<std::vector<int>> nb(topo.points.size());
            for (const auto& e : topo.edges) { nb[(size_t)e.first].push_back(e.second); nb[(size_t)e.second].push_back(e.first); }
            std::vector<glm::vec3> P = topo.points;
            const int iters = std::clamp((int)std::lround(in.param2), 1, 100);
            const float s = std::clamp(k01, 0.0f, 1.0f);
            for (int it = 0; it < iters; ++it) {
                std::vector<glm::vec3> Q = P;
                for (size_t i = 0; i < P.size(); ++i) {
                    if (nb[i].empty()) continue;
                    glm::vec3 avg(0.0f);
                    for (int j : nb[i]) avg += P[(size_t)j];
                    Q[i] = glm::mix(P[i], avg / (float)nb[i].size(), s);
                }
                P.swap(Q);
            }
            for (size_t i = 0; i < out.vertices.size(); ++i) out.vertices[i].position = P[(size_t)topo.vertexPoint[i]];
            recomputeNormals(out);
            break;
        }
        case 26: {  // Malla (jaula)
            if (!in.second || !in.rest || in.rest->vertices.size() != in.second->vertices.size()) break;
            std::vector<glm::vec3> rest, now;
            for (size_t i = 0; i < in.rest->vertices.size(); ++i) {
                rest.push_back(glm::vec3(in.secondMatrix * glm::vec4(in.rest->vertices[i].position, 1.0f)));
                now.push_back(glm::vec3(in.secondMatrix * glm::vec4(in.second->vertices[i].position, 1.0f)));
            }
            if (rest.size() > 4000) break;
            for (Vertex& v : out.vertices) {
                glm::vec3 d(0.0f);
                float ws = 0.0f;
                for (size_t i = 0; i < rest.size(); ++i) {
                    const float dd = glm::length(v.position - rest[i]) + 1e-5f;
                    const float w = 1.0f / (dd * dd * dd);
                    d += w * (now[i] - rest[i]);
                    ws += w;
                }
                if (ws > 0) v.position += d / ws * k01;
            }
            recomputeNormals(out);
            break;
        }
        case 28: {  // Booleano
            if (!in.second) break;
            MeshData bm = *in.second;
            for (Vertex& v : bm.vertices) {
                v.position = glm::vec3(in.secondMatrix * glm::vec4(v.position, 1.0f));
                v.normal = glm::normalize(glm::mat3(in.secondMatrix) * v.normal);
            }
            const int m = std::clamp((int)std::lround(in.param2), 0, 2);
            MeshData r;
            if (booleanMeshes(out, bm, m == 0 ? BoolOp::Union : m == 1 ? BoolOp::Subtract : BoolOp::Intersect, r)) {
                updateBounds(r);
                return r;
            }
            break;
        }
        case 29: {  // Conector
            MeshData r;
            for (const auto& [mesh, xf] : in.all) appendMesh(r, *mesh, xf);
            if (r.vertices.empty()) break;
            weldExact(r, std::max(amount, 0.001f) / kCm);
            r.faceTris.clear();
            recomputeNormals(r);
            updateBounds(r);
            return r;
        }
        case 30: {  // Reduccion de poligonos: agrupacion en rejilla
            const size_t triCount = out.indices.size() / 3;
            const size_t target = std::max<size_t>(4, (size_t)(triCount * std::clamp(k01, 0.01f, 1.0f)));
            if (target >= triCount) break;
            float lo = 1e-4f * glm::length(size), hi = glm::length(size);
            MeshData best;
            for (int it = 0; it < 18; ++it) {
                const float cell = 0.5f * (lo + hi);
                MeshData m = out;
                for (Vertex& v : m.vertices) v.position = (glm::floor((v.position - mn) / cell) + 0.5f) * cell + mn;
                weldExact(m, cell * 0.01f);
                if (m.indices.size() / 3 > target) lo = cell;
                else { hi = cell; best = std::move(m); }
            }
            if (best.indices.empty()) break;
            best.faceTris.clear();
            recomputeNormals(best);
            updateBounds(best);
            return best;
        }
        case 31: {  // Formacion de Atomos
            MeshTopology topo = buildMeshTopology(out);
            if (topo.edges.size() > 6000) break;
            const float rs = std::max(in.param2, 0.01f) / kCm, rc = std::max(amount, 0.01f) / kCm;
            const MeshData sph = makeSphereMesh(6, 10), cyl = makeCylinderMesh(0.5f, 1.0f, 8);
            MeshData r;
            for (const glm::vec3& p : topo.points)
                appendMesh(r, sph, glm::scale(glm::translate(glm::mat4(1.0f), p), glm::vec3(rs * 2.0f)));
            for (const auto& e : topo.edges) {
                const glm::vec3 p0 = topo.points[(size_t)e.first], p1 = topo.points[(size_t)e.second];
                const glm::vec3 d = p1 - p0;
                const float len = glm::length(d);
                if (len < 1e-7f) continue;
                const glm::vec3 y = d / len;
                const glm::vec3 x = glm::normalize(glm::cross(std::fabs(y.y) < 0.99f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0), y));
                const glm::vec3 z = glm::cross(x, y);
                glm::mat4 m(glm::vec4(x * rc * 2.0f, 0), glm::vec4(y * len, 0), glm::vec4(z * rc * 2.0f, 0), glm::vec4((p0 + p1) * 0.5f, 1));
                appendMesh(r, cyl, m);
            }
            updateBounds(r);
            return r;
        }
        case 32: {  // Metaball
            if (in.spheres.empty()) return MeshData{};
            glm::vec3 bmn(1e30f), bmx(-1e30f);
            for (const auto& s : in.spheres) { bmn = glm::min(bmn, s.first - glm::vec3(s.second * 2.2f)); bmx = glm::max(bmx, s.first + glm::vec3(s.second * 2.2f)); }
            float cell = std::max(in.param2, 0.5f) / kCm;
            for (;;) { const glm::vec3 gc = glm::ceil((bmx - bmn) / cell); if (gc.x * gc.y * gc.z <= 1.5e6f) break; cell *= 1.25f; }
            MeshData r;
            marchTets(in.spheres, bmn, bmx, cell, r);
            weldExact(r, cell * 1e-3f);
            recomputeNormals(r);
            updateBounds(r);
            return r;
        }
        case 33: {  // LOD
            if (in.all.empty()) break;
            const float d = glm::length(in.viewPointNode) * kCm / std::max(0.01f, g_lodDetail);
            const size_t lvl = std::min(in.all.size() - 1, (size_t)std::max(0.0f, d / std::max(amount, 1.0f)));
            MeshData r;
            appendMesh(r, *in.all[lvl].first, in.all[lvl].second);
            updateBounds(r);
            return r;
        }
        case 34: {  // Matriz (circulo)
            const int n = std::clamp((int)std::lround(in.param2), 1, 500);
            const float rad = amount / kCm;
            MeshData r;
            for (int i = 0; i < n; ++i) {
                const float ang = 2.0f * kPi * i / n;
                glm::vec3 off(0.0f);
                off[b] = rad * std::cos(ang);
                off[c] = rad * std::sin(ang);
                appendMesh(r, out, glm::translate(glm::mat4(1.0f), off) * rotAxisMat(a, -ang));
            }
            updateBounds(r);
            return r;
        }
        case 35: {  // Matriz MoGraph
            const int n = std::clamp((int)std::lround(in.param2), 1, 30);
            const float sp = amount / kCm;
            const MeshData cube = makeCubeMesh();
            MeshData r;
            for (int z = 0; z < n; ++z)
                for (int y = 0; y < n; ++y)
                    for (int x = 0; x < n; ++x) {
                        const glm::vec3 p = (glm::vec3(x, y, z) - 0.5f * (n - 1)) * sp;
                        appendMesh(r, cube, glm::scale(glm::translate(glm::mat4(1.0f), p), glm::vec3(sp * 0.2f)));
                    }
            updateBounds(r);
            return r;
        }
        case 36:    // Fractura
        case 37: {  // Fractura Voronoi
            MeshTopology topo = buildMeshTopology(out);
            std::vector<int> comp;
            int count = 0;
            if (kind == 36) comp = polyComponents(topo, count);
            else {
                count = std::clamp((int)std::lround(in.param2), 2, 500);
                std::vector<glm::vec3> seeds;
                for (int i = 0; i < count; ++i) seeds.push_back(mn + glm::vec3(rnd(20, i), rnd(21, i), rnd(22, i)) * size);
                comp.assign(topo.polys.size(), 0);
                for (size_t f = 0; f < topo.polys.size(); ++f) {
                    if (topo.polys[f].empty()) continue;
                    const glm::vec3 pc = polyCenter(topo, (int)f);
                    float bd = 1e30f;
                    for (int i = 0; i < count; ++i) {
                        const float d = glm::dot(pc - seeds[i], pc - seeds[i]);
                        if (d < bd) { bd = d; comp[f] = i; }
                    }
                }
            }
            std::vector<glm::vec3> cc(count, glm::vec3(0.0f));
            std::vector<int> cn(count, 0);
            for (size_t f = 0; f < topo.polys.size(); ++f) { cc[comp[f]] += polyCenter(topo, (int)f); cn[comp[f]]++; }
            for (int i = 0; i < count; ++i) if (cn[i]) cc[i] /= (float)cn[i];
            std::vector<int> vp;
            MeshData r = unweld(out, topo, vp);
            const float dist = amount / kCm;
            for (size_t i = 0; i < r.vertices.size(); ++i) {
                if (vp[i] < 0) continue;
                const int ci = comp[(size_t)vp[i]];
                glm::vec3 dir = cc[ci] - ctr;
                if (glm::length(dir) > 1e-9f) r.vertices[i].position += glm::normalize(dir) * dist;
            }
            updateBounds(r);
            return r;
        }
        case 38: {  // MoInstance
            if (!in.motion || in.motion->empty()) break;
            const int n = std::clamp((int)std::lround(in.param2), 1, 200);
            const glm::vec3 now = in.motion->evaluate(in.frame);
            MeshData r = out;
            for (int i = 1; i <= n; ++i) {
                const glm::vec3 then = in.motion->evaluate(in.frame - i * std::max(amount, 0.1f));
                const glm::vec3 off = glm::mat3(in.parentToNode) * (then - now);
                appendMesh(r, out, glm::translate(glm::mat4(1.0f), off));
            }
            updateBounds(r);
            return r;
        }
        case 39: {  // MoExtrude: cada poligono por separado, con paredes en `steps` tramos
            const int steps = std::clamp((int)std::lround(in.param2), 1, 20);
            const float dist = amount / kCm;
            MeshTopology topo = buildMeshTopology(out);
            MeshData r;
            auto addV = [&](const glm::vec3& p, const glm::vec3& n) {
                r.vertices.push_back({p, glm::vec3(0.8f), n, glm::vec2(0.0f)});
                return (uint32_t)r.vertices.size() - 1;
            };
            for (size_t f = 0; f < topo.polys.size(); ++f) {
                const auto& poly = topo.polys[f];
                if (poly.size() < 3) continue;
                const glm::vec3 n = topo.polyNormals[f];
                const size_t m = poly.size();
                for (int st = 0; st < steps; ++st) {
                    const float d0 = dist * st / steps, d1 = dist * (st + 1) / steps;
                    for (size_t i = 0; i < m; ++i) {
                        const glm::vec3 p0 = topo.points[(size_t)poly[i]], p1 = topo.points[(size_t)poly[(i + 1) % m]];
                        const glm::vec3 wn = glm::normalize(glm::cross(p1 - p0, n) + 1e-12f);
                        const uint32_t a0 = addV(p0 + n * d0, wn), a1 = addV(p1 + n * d0, wn);
                        const uint32_t b1 = addV(p1 + n * d1, wn), b0 = addV(p0 + n * d1, wn);
                        r.indices.insert(r.indices.end(), {a0, a1, b1, a0, b1, b0});
                        r.faceTris.push_back(2);
                    }
                }
                const uint32_t base = (uint32_t)r.vertices.size();
                for (size_t i = 0; i < m; ++i) addV(topo.points[(size_t)poly[i]] + n * dist, n);
                for (size_t i = 1; i + 1 < m; ++i) r.indices.insert(r.indices.end(), {base, base + (uint32_t)i, base + (uint32_t)i + 1});
                r.faceTris.push_back((uint32_t)(m - 2));
            }
            updateBounds(r);
            return r;
        }
        case 27:    // Spline: dos splines (original, modificado)
        case 49:    // Rail de Splines
        case 50: {  // Envolver Spline
            if (in.splines.empty() || in.splines[0].empty()) break;
            const SplineContour& s0 = in.splines[0][0];
            if (kind == 27) {
                if (in.splines.size() < 2 || in.splines[1].empty()) break;
                const SplineContour orig = resampleContour(s0, 200), mod = resampleContour(in.splines[1][0], 200);
                const float rad = std::max(amount, 0.1f) / kCm;
                for (Vertex& v : out.vertices) {
                    size_t bi = 0;
                    float bd = 1e30f;
                    for (size_t i = 0; i < orig.pts.size(); ++i) {
                        const float d = glm::length(v.position - orig.pts[i]);
                        if (d < bd) { bd = d; bi = i; }
                    }
                    const float w = std::clamp(1.0f - bd / rad, 0.0f, 1.0f);
                    v.position += (mod.pts[bi] - orig.pts[bi]) * (w * w * (3 - 2 * w));
                }
            } else if (kind == 49) {
                // ancho (eje b) a cada altura (eje a) = |coordenada b| del rail a esa altura
                const SplineContour r = resampleContour(s0, 200);
                float maxB = 1e-6f;
                for (const Vertex& v : out.vertices) maxB = std::max(maxB, std::fabs(v.position[b] - ctr[b]));
                for (Vertex& v : out.vertices) {
                    float bd = 1e30f, width = maxB;
                    for (const glm::vec3& q : r.pts) {
                        const float d = std::fabs(q[a] - v.position[a]);
                        if (d < bd) { bd = d; width = std::fabs(q[b] - ctr[b]); }
                    }
                    const float k = glm::mix(1.0f, width / maxB, std::clamp(k01, 0.0f, 1.0f));
                    v.position[b] = ctr[b] + (v.position[b] - ctr[b]) * k;
                    v.position[c] = ctr[c] + (v.position[c] - ctr[c]) * k;
                }
            } else {
                // eje a del objeto -> longitud del spline; (b, c) -> desplazamiento en el marco del spline
                const SplineContour path = resampleContour(s0, 256);
                const int K = (int)path.pts.size();
                std::vector<glm::vec3> T(K), N(K), B(K);
                for (int i = 0; i < K; ++i) {
                    const glm::vec3 p0 = path.pts[(size_t)std::max(0, i - 1)], p1 = path.pts[(size_t)std::min(K - 1, i + 1)];
                    T[i] = glm::length(p1 - p0) > 1e-12f ? glm::normalize(p1 - p0) : glm::vec3(1, 0, 0);
                }
                glm::vec3 up(0.0f);
                up[b] = 1.0f;
                N[0] = glm::length(glm::cross(T[0], up)) > 1e-6f ? glm::normalize(up - glm::dot(up, T[0]) * T[0]) : glm::vec3(0, 0, 1);
                for (int i = 1; i < K; ++i) {
                    const glm::vec3 n = N[i - 1] - glm::dot(N[i - 1], T[i]) * T[i];
                    N[i] = glm::length(n) > 1e-9f ? glm::normalize(n) : N[i - 1];
                }
                for (int i = 0; i < K; ++i) B[i] = glm::cross(T[i], N[i]);
                for (Vertex& v : out.vertices) {
                    const float tl = std::clamp((v.position[a] - mn[a]) / size[a], 0.0f, 1.0f);
                    const float fi = tl * (K - 1);
                    const int i0 = std::min(K - 2, (int)fi);
                    const float fr = fi - i0;
                    const glm::vec3 P = glm::mix(path.pts[(size_t)i0], path.pts[(size_t)i0 + 1], fr);
                    const glm::vec3 Nn = glm::normalize(glm::mix(N[i0], N[i0 + 1], fr)), Bn = glm::normalize(glm::mix(B[i0], B[i0 + 1], fr));
                    const glm::vec3 np = P + Nn * (v.position[b] - ctr[b]) + Bn * (v.position[c] - ctr[c]);
                    v.position = glm::mix(v.position, np, std::clamp(k01, 0.0f, 1.0f));
                }
            }
            recomputeNormals(out);
            break;
        }
        case 41: {  // Extrusion
            std::vector<SplineContour> all;
            for (const auto& sp : in.splines) all.insert(all.end(), sp.begin(), sp.end());
            if (all.empty()) return MeshData{};
            glm::vec3 off(0.0f);
            off[axis == 1 ? 2 : axis == 2 ? 1 : 0] = 0.0f;
            off[std::clamp(axis, 0, 2)] = amount / kCm;
            return extrudeContours(all, off, (int)std::lround(in.param2), true);
        }
        case 42: {  // Torno
            if (in.splines.empty() || in.splines[0].empty()) return MeshData{};
            return latheContour(in.splines[0][0], amount, (int)std::lround(in.param2));
        }
        case 43: {  // Forro
            std::vector<SplineContour> rings;
            for (const auto& sp : in.splines) if (!sp.empty()) rings.push_back(sp[0]);
            if (rings.size() < 2) return MeshData{};
            return loftContours(rings, (int)std::lround(in.param2), true);
        }
        case 44: {  // Recorrido
            if (in.splinesLocal.size() < 2 || in.splinesLocal[0].empty() || in.splines[1].empty()) return MeshData{};
            return sweepContour(in.splinesLocal[0][0], in.splines[1][0], amount / 100.0f, in.param2, true);
        }
        case 45: {  // MoText
            const std::string txt = in.formula ? *in.formula : std::string("MoText");
            auto cs = textContours(txt, amount / kCm);
            if (cs.empty()) return MeshData{};
            float maxX = 0;
            for (const auto& cc : cs) for (const glm::vec3& p : cc.pts) maxX = std::max(maxX, p.x);
            for (auto& cc : cs) for (glm::vec3& p : cc.pts) p.x -= maxX * 0.5f;  // centrado
            return extrudeContours(cs, glm::vec3(0, 0, -std::max(in.param2, 0.0f) / kCm), 1, true);
        }
        case 46: {  // Trazador
            if (!in.splineOut) break;
            const int len = std::clamp((int)std::lround(in.param2), 1, 10000);
            for (size_t i = 0; i < in.childMotions.size(); ++i) {
                const AnimTrack* tr = in.childMotions[i];
                if (!tr || tr->empty()) continue;
                SplineContour sc;
                const float f0 = std::max((float)tr->keys.front().frame, in.frame - len);
                const int n = std::max(2, (int)std::ceil(in.frame - f0) + 1);
                for (int k = 0; k < n; ++k) sc.pts.push_back(tr->evaluate(f0 + (in.frame - f0) * k / (n - 1)));
                in.splineOut->push_back(std::move(sc));
            }
            return MeshData{};
        }
        case 47: {  // MoSpline
            if (!in.splineOut) break;
            if (!in.splines.empty() && !in.splines[0].empty()) {
                for (const auto& cc : in.splines[0]) in.splineOut->push_back(contourPart(cc, 0.0f, std::clamp(k01, 0.0f, 1.0f)));
            } else {
                SplineContour sc;
                const int segs = std::clamp((int)std::lround(in.param2), 2, 2000);
                glm::vec3 p(0.0f), dir(0, 1, 0);
                const float step = 2.0f / segs;  // 400 cm en total
                sc.pts.push_back(p);
                for (int i = 0; i < segs * std::clamp(k01, 0.0f, 1.0f); ++i) {
                    const float ang = glm::radians(15.0f);
                    dir = glm::normalize(glm::vec3(dir.x * std::cos(ang) - dir.z * std::sin(ang), dir.y, dir.x * std::sin(ang) + dir.z * std::cos(ang)) + glm::vec3(0.12f, 0, 0));
                    p += dir * step;
                    sc.pts.push_back(p);
                }
                in.splineOut->push_back(std::move(sc));
            }
            return MeshData{};
        }
        case 51: {  // Bezier: 16 puntos de control (in.rest)
            std::vector<glm::vec3> cp(16);
            for (int j = 0; j < 4; ++j)
                for (int i = 0; i < 4; ++i) cp[(size_t)(j * 4 + i)] = glm::vec3((i / 3.0f - 0.5f) * 2.0f, 0.0f, (j / 3.0f - 0.5f) * 2.0f);
            if (in.rest && in.rest->vertices.size() == 16)
                for (int k = 0; k < 16; ++k) cp[(size_t)k] = in.rest->vertices[(size_t)k].position;
            const int sub = std::clamp((int)std::lround(in.param2), 1, 128);
            auto bern = [](int i, float t) {
                const float u = 1 - t;
                return i == 0 ? u * u * u : i == 1 ? 3 * u * u * t : i == 2 ? 3 * u * t * t : t * t * t;
            };
            MeshData r;
            for (int y = 0; y <= sub; ++y)
                for (int x = 0; x <= sub; ++x) {
                    const float u = (float)x / sub, v = (float)y / sub;
                    glm::vec3 p(0.0f);
                    for (int j = 0; j < 4; ++j)
                        for (int i = 0; i < 4; ++i) p += cp[(size_t)(j * 4 + i)] * bern(i, u) * bern(j, v);
                    r.vertices.push_back({p, glm::vec3(0.8f), glm::vec3(0, 1, 0), glm::vec2(u, v)});
                }
            for (int y = 0; y < sub; ++y)
                for (int x = 0; x < sub; ++x) {
                    const uint32_t a0 = (uint32_t)(y * (sub + 1) + x), a1 = a0 + 1, b0 = a0 + (uint32_t)(sub + 1), b1 = b0 + 1;
                    r.indices.insert(r.indices.end(), {a0, b0, b1, a0, b1, a1});
                    r.faceTris.push_back(2);
                }
            recomputeNormals(r);
            updateBounds(r);
            return r;
        }
        case 48: {  // Mascara de Spline
            if (!in.splineOut || in.splines.empty()) break;
            std::vector<std::vector<SplineContour>> ins;
            for (const auto& sp : in.splines) if (!sp.empty()) ins.push_back(sp);
            if (ins.empty()) break;
            *in.splineOut = splineBoolean(ins, std::clamp((int)std::lround(in.param2), 0, 4));
            return MeshData{};
        }
        default: break;
    }
    updateBounds(out);
    return out;
}

} // namespace iron3d
