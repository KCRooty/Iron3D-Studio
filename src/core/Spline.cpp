#include "Spline.h"

#include "DeformMore.h"  // evalFormula

#include <stb_image.h>
#include <stb_truetype.h>

#include <algorithm>
#include <functional>
#include <tuple>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>

namespace iron3d {

namespace {
constexpr float kCm = 200.0f;
constexpr float kPi = 3.14159265358979f;

glm::vec3 toPlane(int plane, float u, float v, float w) {
    switch (plane) {
        case 1: return {w, v, u};   // ZY
        case 2: return {u, w, v};   // XZ (en el suelo)
        default: return {u, v, w};  // XY
    }
}

void catmull(const std::vector<glm::vec3>& p, bool closed, int sub, std::vector<glm::vec3>& out) {
    const int n = (int)p.size();
    if (n < 3) { out = p; return; }
    auto at = [&](int i) {
        if (closed) return p[(size_t)((i % n + n) % n)];
        return p[(size_t)std::clamp(i, 0, n - 1)];
    };
    const int segs = closed ? n : n - 1;
    for (int i = 0; i < segs; ++i) {
        const glm::vec3 p0 = at(i - 1), p1 = at(i), p2 = at(i + 1), p3 = at(i + 2);
        for (int s = 0; s < sub; ++s) {
            const float t = (float)s / sub, t2 = t * t, t3 = t2 * t;
            out.push_back(0.5f * ((2.0f * p1) + (-p0 + p2) * t + (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2 +
                                  (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3));
        }
    }
    if (!closed) out.push_back(p.back());
}

SplineContour ring(int plane, int n, const std::function<glm::vec2(float)>& f, bool closed = true) {
    SplineContour c;
    c.closed = closed;
    for (int i = 0; i < n; ++i) {
        const float t = closed ? (float)i / n : (float)i / (n - 1);
        const glm::vec2 q = f(t);
        c.pts.push_back(toPlane(plane, q.x, q.y, 0.0f));
    }
    return c;
}

// --- Fuente ---
struct Font {
    std::vector<unsigned char> data;
    stbtt_fontinfo info{};
    bool ok = false;
};
Font& sysFont() {
    static Font f;
    static bool tried = false;
    if (tried) return f;
    tried = true;
    const char* paths[] = {"C:/Windows/Fonts/arial.ttf", "C:/Windows/Fonts/segoeui.ttf", "C:/Windows/Fonts/tahoma.ttf",
                           "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", "/usr/share/fonts/TTF/DejaVuSans.ttf",
                           "/usr/share/fonts/dejavu/DejaVuSans.ttf", "/usr/share/fonts/liberation/LiberationSans-Regular.ttf",
                           "/System/Library/Fonts/Supplemental/Arial.ttf"};
    for (const char* p : paths) {
        std::ifstream in(p, std::ios::binary);
        if (!in) continue;
        f.data.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        if (!f.data.empty() && stbtt_InitFont(&f.info, f.data.data(), stbtt_GetFontOffsetForIndex(f.data.data(), 0))) {
            f.ok = true;
            break;
        }
    }
    return f;
}

std::vector<uint32_t> utf8Decode(const std::string& s) {
    std::vector<uint32_t> out;
    for (size_t i = 0; i < s.size();) {
        const unsigned char c = (unsigned char)s[i];
        uint32_t cp = c;
        int extra = 0;
        if (c >= 0xF0) { cp = c & 0x07; extra = 3; }
        else if (c >= 0xE0) { cp = c & 0x0F; extra = 2; }
        else if (c >= 0xC0) { cp = c & 0x1F; extra = 1; }
        ++i;
        for (int k = 0; k < extra && i < s.size(); ++k, ++i) cp = (cp << 6) | ((unsigned char)s[i] & 0x3F);
        out.push_back(cp);
    }
    return out;
}

double area2(const std::vector<glm::dvec2>& p) {
    double a = 0;
    for (size_t i = 0, j = p.size() - 1; i < p.size(); j = i++) a += (p[j].x - p[i].x) * (p[j].y + p[i].y);
    return a;  // > 0 = antihorario
}
bool pointInPoly(const glm::dvec2& q, const std::vector<glm::dvec2>& p) {
    bool in = false;
    for (size_t i = 0, j = p.size() - 1; i < p.size(); j = i++)
        if (((p[i].y > q.y) != (p[j].y > q.y)) && (q.x < (p[j].x - p[i].x) * (q.y - p[i].y) / (p[j].y - p[i].y) + p[i].x)) in = !in;
    return in;
}
double cross2(const glm::dvec2& o, const glm::dvec2& a, const glm::dvec2& b) { return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x); }
bool segCross(const glm::dvec2& a, const glm::dvec2& b, const glm::dvec2& c, const glm::dvec2& d) {
    const double d1 = cross2(c, d, a), d2 = cross2(c, d, b), d3 = cross2(a, b, c), d4 = cross2(a, b, d);
    return ((d1 > 1e-12 && d2 < -1e-12) || (d1 < -1e-12 && d2 > 1e-12)) && ((d3 > 1e-12 && d4 < -1e-12) || (d3 < -1e-12 && d4 > 1e-12));
}

// Ear clipping de un poligono simple antihorario (indices a `pts`).
void earClip(const std::vector<glm::dvec2>& pts, std::vector<int> poly, std::vector<uint32_t>& tris) {
    auto inTri = [&](const glm::dvec2& p, const glm::dvec2& a, const glm::dvec2& b, const glm::dvec2& c) {
        return cross2(a, b, p) > 1e-14 && cross2(b, c, p) > 1e-14 && cross2(c, a, p) > 1e-14;
    };
    int guard = 0;
    while (poly.size() > 3 && guard++ < 100000) {
        const size_t n = poly.size();
        bool clipped = false;
        for (size_t i = 0; i < n; ++i) {
            const int ia = poly[(i + n - 1) % n], ib = poly[i], ic = poly[(i + 1) % n];
            const glm::dvec2 &a = pts[(size_t)ia], &b = pts[(size_t)ib], &c = pts[(size_t)ic];
            if (cross2(a, b, c) <= 1e-14) continue;  // reflejo o plano
            bool ear = true;
            for (size_t k = 0; k < n && ear; ++k) {
                const int q = poly[k];
                if (q == ia || q == ib || q == ic) continue;
                const glm::dvec2& p = pts[(size_t)q];
                if (p == a || p == b || p == c) continue;
                if (inTri(p, a, b, c)) ear = false;
            }
            if (!ear) continue;
            tris.insert(tris.end(), {(uint32_t)ia, (uint32_t)ib, (uint32_t)ic});
            poly.erase(poly.begin() + (long)i);
            clipped = true;
            break;
        }
        if (!clipped) {  // poligono degenerado: se quita un vertice para no bloquearse
            const size_t n2 = poly.size();
            tris.insert(tris.end(), {(uint32_t)poly[n2 - 1], (uint32_t)poly[0], (uint32_t)poly[1]});
            poly.erase(poly.begin());
        }
    }
    if (poly.size() == 3) tris.insert(tris.end(), {(uint32_t)poly[0], (uint32_t)poly[1], (uint32_t)poly[2]});
}

// Triangula contornos 2D con agujeros (paridad). Devuelve indices sobre `pts` (todos los puntos concatenados).
void triangulate2D(const std::vector<std::vector<glm::dvec2>>& contours, std::vector<glm::dvec2>& pts, std::vector<uint32_t>& tris) {
    const size_t nc = contours.size();
    std::vector<int> depth(nc, 0), parent(nc, -1);
    std::vector<double> areaAbs(nc);
    for (size_t i = 0; i < nc; ++i) areaAbs[i] = std::fabs(area2(contours[i]));
    for (size_t i = 0; i < nc; ++i) {
        if (contours[i].size() < 3) continue;
        double best = 1e300;
        for (size_t j = 0; j < nc; ++j) {
            if (i == j || contours[j].size() < 3) continue;
            if (pointInPoly(contours[i][0], contours[j])) {
                ++depth[i];
                if (areaAbs[j] < best) { best = areaAbs[j]; parent[i] = (int)j; }
            }
        }
    }
    std::vector<int> base(nc);
    pts.clear();
    for (size_t i = 0; i < nc; ++i) {
        base[i] = (int)pts.size();
        pts.insert(pts.end(), contours[i].begin(), contours[i].end());
    }
    for (size_t o = 0; o < nc; ++o) {
        if (contours[o].size() < 3 || depth[o] % 2) continue;
        std::vector<int> poly;
        for (size_t k = 0; k < contours[o].size(); ++k) poly.push_back(base[o] + (int)k);
        if (area2(contours[o]) < 0) std::reverse(poly.begin(), poly.end());
        // agujeros de este contorno, por x maxima descendente
        std::vector<std::pair<double, size_t>> holes;
        for (size_t h = 0; h < nc; ++h)
            if (parent[h] == (int)o && depth[h] % 2 && contours[h].size() >= 3) {
                double mx = -1e300;
                for (const auto& p : contours[h]) mx = std::max(mx, p.x);
                holes.push_back({mx, h});
            }
        std::sort(holes.begin(), holes.end(), [](auto& a, auto& b) { return a.first > b.first; });
        for (const auto& hh : holes) {
            const size_t h = hh.second;
            std::vector<int> hp;
            for (size_t k = 0; k < contours[h].size(); ++k) hp.push_back(base[h] + (int)k);
            if (area2(contours[h]) > 0) std::reverse(hp.begin(), hp.end());  // agujero horario
            size_t mi = 0;
            for (size_t k = 1; k < hp.size(); ++k) if (pts[(size_t)hp[k]].x > pts[(size_t)hp[mi]].x) mi = k;
            const glm::dvec2 M = pts[(size_t)hp[mi]];
            // vertice visible del poligono mas cercano
            size_t bestV = 0;
            double bestD = 1e300;
            for (size_t v = 0; v < poly.size(); ++v) {
                const glm::dvec2 P = pts[(size_t)poly[v]];
                const double d = glm::dot(P - M, P - M) + (P.x < M.x ? 1e6 : 0.0);
                if (d >= bestD) continue;
                bool vis = true;
                for (size_t e = 0; e < poly.size() && vis; ++e)
                    if (segCross(M, P, pts[(size_t)poly[e]], pts[(size_t)poly[(e + 1) % poly.size()]])) vis = false;
                for (size_t e = 0; e < hp.size() && vis; ++e)
                    if (segCross(M, P, pts[(size_t)hp[e]], pts[(size_t)hp[(e + 1) % hp.size()]])) vis = false;
                if (vis) { bestD = d; bestV = v; }
            }
            std::vector<int> merged(poly.begin(), poly.begin() + (long)bestV + 1);
            for (size_t k = 0; k <= hp.size(); ++k) merged.push_back(hp[(mi + k) % hp.size()]);
            merged.push_back(poly[bestV]);
            merged.insert(merged.end(), poly.begin() + (long)bestV + 1, poly.end());
            poly.swap(merged);
        }
        earClip(pts, poly, tris);
    }
}

// Base ortonormal del plano de unos contornos (normal de Newell del mayor).
void planeBasis(const std::vector<SplineContour>& cs, glm::vec3& u, glm::vec3& v, glm::vec3& n, glm::vec3& o) {
    n = glm::vec3(0.0f);
    float bestLen = -1.0f;
    for (const auto& c : cs) {
        if (!c.closed || c.pts.size() < 3) continue;
        glm::vec3 nn(0.0f);
        for (size_t i = 0, j = c.pts.size() - 1; i < c.pts.size(); j = i++) {
            const glm::vec3 &a = c.pts[j], &b = c.pts[i];
            nn += glm::vec3((a.y - b.y) * (a.z + b.z), (a.z - b.z) * (a.x + b.x), (a.x - b.x) * (a.y + b.y));
        }
        if (glm::length(nn) > bestLen) { bestLen = glm::length(nn); n = nn; o = c.pts[0]; }
    }
    if (glm::length(n) < 1e-12f) n = glm::vec3(0, 0, 1);
    n = glm::normalize(n);
    u = glm::normalize(glm::cross(std::fabs(n.y) < 0.9f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0), n));
    v = glm::cross(n, u);
}

void smoothByPosition(MeshData& m) {
    std::map<std::tuple<float, float, float>, glm::vec3> acc;
    for (Vertex& v : m.vertices) v.normal = glm::vec3(0.0f);
    for (size_t t = 0; t + 2 < m.indices.size(); t += 3) {
        Vertex &a = m.vertices[m.indices[t]], &b = m.vertices[m.indices[t + 1]], &c = m.vertices[m.indices[t + 2]];
        const glm::vec3 n = glm::cross(b.position - a.position, c.position - a.position);
        for (Vertex* x : {&a, &b, &c}) acc[{x->position.x, x->position.y, x->position.z}] += n;
    }
    for (Vertex& v : m.vertices) {
        const glm::vec3 n = acc[{v.position.x, v.position.y, v.position.z}];
        v.normal = glm::length(n) > 1e-20f ? glm::normalize(n) : glm::vec3(0, 1, 0);
    }
}

Vertex vtx(const glm::vec3& p, const glm::vec3& n = glm::vec3(0, 1, 0), const glm::vec2& uv = glm::vec2(0.0f)) {
    Vertex v{};
    v.position = p;
    v.color = glm::vec3(0.8f);
    v.normal = n;
    v.uv = uv;
    return v;
}

// Marching squares sobre una mascara (w x h), con borde de ceros. Devuelve contornos en coordenadas de celda.
std::vector<std::vector<glm::dvec2>> marchingSquares(const std::vector<char>& mask, int w, int h) {
    auto at = [&](int x, int y) -> int { return (x < 0 || y < 0 || x >= w || y >= h) ? 0 : mask[(size_t)y * w + x]; };
    // puntos medios de arista: id unico por arista entre dos muestras vecinas
    std::map<long long, std::vector<long long>> adj;
    std::map<long long, glm::dvec2> pos;
    auto hid = [&](int x, int y) { return ((long long)(y + 1) * (w + 3) + (x + 1)) * 2; };      // (x,y)-(x+1,y)
    auto vid = [&](int x, int y) { return ((long long)(y + 1) * (w + 3) + (x + 1)) * 2 + 1; };  // (x,y)-(x,y+1)
    auto link = [&](long long a, glm::dvec2 pa, long long b, glm::dvec2 pb) {
        adj[a].push_back(b);
        adj[b].push_back(a);
        pos[a] = pa;
        pos[b] = pb;
    };
    for (int y = -1; y < h; ++y)
        for (int x = -1; x < w; ++x) {
            const int c = at(x, y) | (at(x + 1, y) << 1) | (at(x + 1, y + 1) << 2) | (at(x, y + 1) << 3);
            if (c == 0 || c == 15) continue;
            const long long T = hid(x, y), B = hid(x, y + 1), L = vid(x, y), R = vid(x + 1, y);
            const glm::dvec2 pT(x + 0.5, y), pB(x + 0.5, y + 1), pL(x, y + 0.5), pR(x + 1, y + 0.5);
            switch (c) {
                case 1: case 14: link(L, pL, T, pT); break;
                case 2: case 13: link(T, pT, R, pR); break;
                case 3: case 12: link(L, pL, R, pR); break;
                case 4: case 11: link(R, pR, B, pB); break;
                case 6: case 9: link(T, pT, B, pB); break;
                case 7: case 8: link(L, pL, B, pB); break;
                case 5: link(L, pL, T, pT); link(R, pR, B, pB); break;
                case 10: link(T, pT, R, pR); link(L, pL, B, pB); break;
                default: break;
            }
        }
    std::vector<std::vector<glm::dvec2>> out;
    std::map<long long, bool> used;
    for (auto& [start, nb] : adj) {
        if (used[start]) continue;
        std::vector<glm::dvec2> loop;
        long long prev = -1, cur = start;
        int guard = 0;
        while (guard++ < 10000000) {
            used[cur] = true;
            loop.push_back(pos[cur]);
            const auto& n = adj[cur];
            long long next = -1;
            for (long long q : n) if (q != prev && !used[q]) { next = q; break; }
            if (next < 0) break;
            prev = cur;
            cur = next;
        }
        if (loop.size() >= 3) out.push_back(std::move(loop));
    }
    return out;
}

// Douglas-Peucker cerrado.
std::vector<glm::dvec2> simplifyClosed(const std::vector<glm::dvec2>& p, double tol) {
    if (p.size() < 8) return p;
    std::vector<char> keep(p.size(), 0);
    std::function<void(size_t, size_t)> rec = [&](size_t a, size_t b) {
        double best = -1;
        size_t bi = a;
        const glm::dvec2 A = p[a], B = p[b % p.size()];
        for (size_t i = a + 1; i < b; ++i) {
            const glm::dvec2 P = p[i % p.size()];
            const glm::dvec2 AB = B - A;
            const double L = glm::length(AB);
            const double d = L < 1e-12 ? glm::length(P - A) : std::fabs(AB.x * (A.y - P.y) - AB.y * (A.x - P.x)) / L;
            if (d > best) { best = d; bi = i; }
        }
        if (best > tol) { keep[bi % p.size()] = 1; rec(a, bi); rec(bi, b); }
    };
    const size_t half = p.size() / 2;
    keep[0] = keep[half] = 1;
    rec(0, half);
    rec(half, p.size());
    std::vector<glm::dvec2> out;
    for (size_t i = 0; i < p.size(); ++i) if (keep[i]) out.push_back(p[i]);
    return out;
}
} // namespace

const char* splineKindName(int kind) {
    static const char* n[kSplineKindCount] = {"Spline", "Arco", "Círculo", "Hélice", "n-Lados", "Rectángulo", "Estrella", "Texto",
                                              "4-Lados", "Cisoide", "Rueda Dentada", "Cicloide", "Fórmula", "Flor", "Perfil", "Vectorizador"};
    return kind >= 0 && kind < kSplineKindCount ? n[kind] : "Spline";
}

SplineData defaultSpline(int kind) {
    SplineData s;
    s.present = true;
    s.kind = kind;
    switch (kind) {
        case 1: s.a = 200; s.startDeg = 0; s.endDeg = 90; break;
        case 2: s.a = 200; break;
        case 3: s.a = 200; s.b = 200; s.c = 200; s.n = 2; break;
        case 4: s.a = 200; s.n = 6; break;
        case 5: s.a = 400; s.b = 400; s.c = 0; break;
        case 6: s.a = 200; s.b = 100; s.n = 8; break;
        case 7: s.a = 200; s.text = "Texto"; break;
        case 8: s.a = 400; s.b = 400; break;
        case 9: s.a = 200; break;
        case 10: s.a = 200; s.b = 160; s.n = 12; break;
        case 11: s.a = 50; s.n = 2; break;
        case 12: s.n = 100; break;
        case 13: s.a = 200; s.b = 100; s.n = 6; break;
        case 14: s.a = 200; s.b = 200; s.c = 20; s.n = 0; break;
        case 15: s.a = 400; s.b = 50; s.text.clear(); break;
        default: break;
    }
    return s;
}

std::vector<SplineContour> splineContours(const SplineData& s) {
    std::vector<SplineContour> out;
    if (!s.present) return out;
    const int pl = s.plane;
    const float A = s.a / kCm, B = s.b / kCm, C = s.c / kCm;
    switch (s.kind) {
        case 0: {
            if (!s.points.empty()) {
                SplineContour c;
                c.closed = s.closed && s.points.size() > 2;
                if (s.interp == 1 && s.points.size() >= 3) catmull(s.points, c.closed, 8, c.pts);
                else c.pts = s.points;
                out.push_back(std::move(c));
            }
            break;
        }
        case 1: {
            const float a0 = glm::radians(s.startDeg), a1 = glm::radians(s.endDeg);
            out.push_back(ring(pl, 48, [&](float t) { const float a = a0 + (a1 - a0) * t; return glm::vec2(A * std::cos(a), A * std::sin(a)); }, false));
            break;
        }
        case 2:
            out.push_back(ring(pl, 64, [&](float t) { return glm::vec2(A * std::cos(t * 2 * kPi), A * std::sin(t * 2 * kPi)); }));
            break;
        case 3: {
            SplineContour c;
            const int turns = std::max(1, s.n), n = turns * 48 + 1;
            for (int i = 0; i < n; ++i) {
                const float t = (float)i / (n - 1), ang = t * turns * 2 * kPi, r = A + (B - A) * t;
                c.pts.push_back(toPlane(pl, r * std::cos(ang), r * std::sin(ang), C * t - C * 0.5f));
            }
            out.push_back(std::move(c));
            break;
        }
        case 4: {
            const int n = std::max(3, s.n);
            out.push_back(ring(pl, n, [&](float t) { const float a = t * 2 * kPi + kPi / 2; return glm::vec2(A * std::cos(a), A * std::sin(a)); }));
            break;
        }
        case 5: {
            SplineContour c;
            c.closed = true;
            const float w = A * 0.5f, h = B * 0.5f, r = std::clamp(C, 0.0f, std::min(w, h));
            if (r <= 1e-6f) {
                for (glm::vec2 q : {glm::vec2(-w, -h), glm::vec2(w, -h), glm::vec2(w, h), glm::vec2(-w, h)}) c.pts.push_back(toPlane(pl, q.x, q.y, 0));
            } else {
                const glm::vec2 cs[4] = {{w - r, -h + r}, {w - r, h - r}, {-w + r, h - r}, {-w + r, -h + r}};
                for (int k = 0; k < 4; ++k)
                    for (int i = 0; i <= 6; ++i) {
                        const float a = (k * 90.0f - 90.0f + i * 15.0f) * kPi / 180.0f;
                        c.pts.push_back(toPlane(pl, cs[k].x + r * std::cos(a), cs[k].y + r * std::sin(a), 0));
                    }
            }
            out.push_back(std::move(c));
            break;
        }
        case 6: {
            const int n = std::max(2, s.n);
            out.push_back(ring(pl, n * 2, [&](float t) {
                const int i = (int)std::lround(t * n * 2);
                const float a = t * 2 * kPi + kPi / 2, r = (i % 2) ? B : A;
                return glm::vec2(r * std::cos(a), r * std::sin(a));
            }));
            break;
        }
        case 7: {
            auto cs = textContours(s.text, A);
            for (auto& c : cs)
                for (glm::vec3& p : c.pts) p = toPlane(pl, p.x, p.y, 0);
            out = std::move(cs);
            break;
        }
        case 8: {
            SplineContour c;
            c.closed = true;
            for (glm::vec2 q : {glm::vec2(0, -B / 2), glm::vec2(A / 2, 0), glm::vec2(0, B / 2), glm::vec2(-A / 2, 0)}) c.pts.push_back(toPlane(pl, q.x, q.y, 0));
            out.push_back(std::move(c));
            break;
        }
        case 9: {
            SplineContour c;
            for (int i = 0; i <= 64; ++i) {
                const float th = -1.25f + 2.5f * i / 64.0f;
                const float r = A * std::sin(th) * std::sin(th) / std::max(1e-3f, std::cos(th));
                c.pts.push_back(toPlane(pl, r * std::cos(th), r * std::sin(th), 0));
            }
            out.push_back(std::move(c));
            break;
        }
        case 10: {
            const int teeth = std::max(3, s.n), n = teeth * 4;
            out.push_back(ring(pl, n, [&](float t) {
                const int i = (int)std::lround(t * n) % 4;
                const float a = t * 2 * kPi, r = (i == 1 || i == 2) ? A : B;
                return glm::vec2(r * std::cos(a), r * std::sin(a));
            }));
            break;
        }
        case 11: {
            SplineContour c;
            const int turns = std::max(1, s.n), n = turns * 48 + 1;
            const float span = turns * 2 * kPi * A;
            for (int i = 0; i < n; ++i) {
                const float t = turns * 2 * kPi * i / (n - 1);
                c.pts.push_back(toPlane(pl, A * (t - std::sin(t)) - span * 0.5f, A * (1 - std::cos(t)), 0));
            }
            out.push_back(std::move(c));
            break;
        }
        case 12: {
            SplineContour c;
            const int n = std::clamp(s.n, 2, 5000);
            for (int i = 0; i < n; ++i) {
                const float t = (float)i / (n - 1);
                float x = 0, y = 0, z = 0;
                evalFormula(s.fx, 0, 0, 0, 0, 0, t, x);
                evalFormula(s.fy, 0, 0, 0, 0, 0, t, y);
                evalFormula(s.fz, 0, 0, 0, 0, 0, t, z);
                c.pts.push_back(toPlane(pl, x / kCm, y / kCm, z / kCm));
            }
            c.closed = s.closed;
            out.push_back(std::move(c));
            break;
        }
        case 13: {
            const int n = std::max(1, s.n);
            out.push_back(ring(pl, 32 * n, [&](float t) {
                const float a = t * 2 * kPi, r = B + (A - B) * std::fabs(std::cos(n * a * 0.5f));
                return glm::vec2(r * std::cos(a), r * std::sin(a));
            }));
            break;
        }
        case 14: {  // Perfil: 0 H, 1 L, 2 T, 3 U, 4 Z
            const float w = A / 2, h = B / 2, t = std::clamp(C, 1e-4f, std::min(w, h));
            std::vector<glm::vec2> q;
            switch (std::clamp(s.n, 0, 4)) {
                case 0: q = {{-w, -h}, {w, -h}, {w, -h + t}, {t / 2, -h + t}, {t / 2, h - t}, {w, h - t}, {w, h}, {-w, h}, {-w, h - t}, {-t / 2, h - t}, {-t / 2, -h + t}, {-w, -h + t}}; break;
                case 1: q = {{-w, -h}, {w, -h}, {w, -h + t}, {-w + t, -h + t}, {-w + t, h}, {-w, h}}; break;
                case 2: q = {{-t / 2, -h}, {t / 2, -h}, {t / 2, h - t}, {w, h - t}, {w, h}, {-w, h}, {-w, h - t}, {-t / 2, h - t}}; break;
                case 3: q = {{-w, -h}, {w, -h}, {w, h}, {w - t, h}, {w - t, -h + t}, {-w + t, -h + t}, {-w + t, h}, {-w, h}}; break;
                default: q = {{-w, -h}, {t / 2, -h}, {t / 2, h - t}, {w, h - t}, {w, h}, {-t / 2, h}, {-t / 2, -h + t}, {-w, -h + t}}; break;
            }
            SplineContour c;
            c.closed = true;
            for (const auto& p : q) c.pts.push_back(toPlane(pl, p.x, p.y, 0));
            out.push_back(std::move(c));
            break;
        }
        case 15: {
            auto cs = vectorizeImage(s.text, std::clamp(s.b / 100.0f, 0.0f, 1.0f), A);
            for (auto& c : cs)
                for (glm::vec3& p : c.pts) p = toPlane(pl, p.x, p.y, 0);
            out = std::move(cs);
            break;
        }
        default: break;
    }
    for (size_t i = 0; i < s.extra.size(); ++i) {
        SplineContour c;
        c.pts = s.extra[i];
        c.closed = i < s.extraClosed.size() && s.extraClosed[i];
        out.push_back(std::move(c));
    }
    return out;
}

void buildSplineLines(const SceneNode& root, const SceneNode* selected, std::vector<Vertex>& lines) {
    std::function<void(const SceneNode&, const glm::mat4&, bool)> walk = [&](const SceneNode& n, const glm::mat4& parent, bool pv) {
        const glm::mat4 w = parent * n.localMatrix();
        const bool vis = resolveVisibility(n.editorVis, pv);
        if (vis && n.spline.present) {
            const bool sel = &n == selected;
            const glm::vec3 col = sel ? glm::vec3(1.0f, 0.85f, 0.2f) : n.spline.generated ? glm::vec3(0.55f, 0.85f, 1.0f) : glm::vec3(0.9f, 0.92f, 0.95f);
            for (const auto& c : splineContours(n.spline)) {
                const size_t m = c.pts.size();
                if (m < 2) continue;
                const size_t segs = c.closed ? m : m - 1;
                for (size_t i = 0; i < segs; ++i) {
                    for (const glm::vec3& p : {c.pts[i], c.pts[(i + 1) % m]}) {
                        Vertex v{};
                        v.position = glm::vec3(w * glm::vec4(p, 1.0f));
                        v.color = col;
                        v.normal = glm::vec3(0, 0, 1);
                        lines.push_back(v);
                    }
                }
            }
            if (sel && n.spline.kind == 0) {  // puntos de control: cruces pequenas
                const float r = 0.02f;
                for (const glm::vec3& p : n.spline.points) {
                    const glm::vec3 c = glm::vec3(w * glm::vec4(p, 1.0f));
                    for (int ax = 0; ax < 3; ++ax) {
                        glm::vec3 d(0.0f);
                        d[ax] = r;
                        for (const glm::vec3& q : {c - d, c + d}) {
                            Vertex v{};
                            v.position = q;
                            v.color = glm::vec3(1.0f, 0.45f, 0.1f);
                            lines.push_back(v);
                        }
                    }
                }
            }
        }
        for (const auto& ch : n.children()) walk(*ch, w, vis);
    };
    walk(root, glm::mat4(1.0f), true);
}

bool rayHitsSpline(const SceneNode& node, const glm::mat4& world, const glm::vec3& o, const glm::vec3& d, float& dist) {
    if (!node.spline.present) return false;
    bool hit = false;
    float best = 1e30f;
    for (const auto& c : splineContours(node.spline)) {
        const size_t m = c.pts.size();
        if (m < 2) continue;
        const size_t segs = c.closed ? m : m - 1;
        for (size_t i = 0; i < segs; ++i) {
            const glm::vec3 a = glm::vec3(world * glm::vec4(c.pts[i], 1.0f)), b = glm::vec3(world * glm::vec4(c.pts[(i + 1) % m], 1.0f));
            const glm::vec3 e = b - a, w0 = o - a;
            const float A = glm::dot(d, d), B = glm::dot(d, e), C = glm::dot(e, e);
            const float D = glm::dot(d, w0), E = glm::dot(e, w0);
            const float den = A * C - B * B;
            float s = den > 1e-12f ? (A * E - B * D) / den : 0.0f;
            s = glm::clamp(s, 0.0f, 1.0f);
            float t = (B * s - D) / A;
            if (t < 0.0f) { t = 0.0f; s = C > 1e-12f ? glm::clamp(E / C, 0.0f, 1.0f) : 0.0f; }
            const float gap = glm::length((o + d * t) - (a + e * s));
            if (gap <= glm::max(0.01f, 0.012f * t) && t < best) { best = t; hit = true; }
        }
    }
    if (hit) dist = best;
    return hit;
}

std::vector<SplineContour> textContours(const std::string& utf8, float height, float spacing) {
    std::vector<SplineContour> out;
    Font& f = sysFont();
    if (!f.ok || utf8.empty()) return out;
    int asc = 0, desc = 0, gap = 0;
    stbtt_GetFontVMetrics(&f.info, &asc, &desc, &gap);
    const float scale = height / std::max(1, asc - desc);
    float penX = 0.0f, penY = 0.0f;
    const std::vector<uint32_t> cps = utf8Decode(utf8);
    for (size_t ci = 0; ci < cps.size(); ++ci) {
        const uint32_t cp = cps[ci];
        if (cp == '\n') { penX = 0; penY -= (asc - desc + gap) * scale; continue; }
        stbtt_vertex* v = nullptr;
        const int nv = stbtt_GetCodepointShape(&f.info, (int)cp, &v);
        SplineContour cur;
        cur.closed = true;
        auto flush = [&]() {
            if (cur.pts.size() > 2) {
                if (glm::length(cur.pts.front() - cur.pts.back()) < 1e-7f) cur.pts.pop_back();
                out.push_back(cur);
            }
            cur.pts.clear();
        };
        glm::vec2 last(0.0f);
        for (int i = 0; i < nv; ++i) {
            const glm::vec2 p(v[i].x, v[i].y), c0(v[i].cx, v[i].cy), c1(v[i].cx1, v[i].cy1);
            auto put = [&](glm::vec2 q) { cur.pts.push_back(glm::vec3(penX + q.x * scale, penY + q.y * scale, 0.0f)); };
            if (v[i].type == STBTT_vmove) { flush(); put(p); }
            else if (v[i].type == STBTT_vline) put(p);
            else if (v[i].type == STBTT_vcurve) {
                for (int s = 1; s <= 6; ++s) { const float t = s / 6.0f; put((1 - t) * (1 - t) * last + 2 * (1 - t) * t * c0 + t * t * p); }
            } else if (v[i].type == STBTT_vcubic) {
                for (int s = 1; s <= 8; ++s) {
                    const float t = s / 8.0f, u = 1 - t;
                    put(u * u * u * last + 3 * u * u * t * c0 + 3 * u * t * t * c1 + t * t * t * p);
                }
            }
            last = p;
        }
        flush();
        if (v) stbtt_FreeShape(&f.info, v);
        int adv = 0, lsb = 0;
        stbtt_GetCodepointHMetrics(&f.info, (int)cp, &adv, &lsb);
        penX += adv * scale + spacing;
        if (ci + 1 < cps.size()) penX += stbtt_GetCodepointKernAdvance(&f.info, (int)cp, (int)cps[ci + 1]) * scale;
    }
    return out;
}

std::vector<SplineContour> splineBoolean(const std::vector<std::vector<SplineContour>>& inputs, int op, int res) {
    std::vector<SplineContour> out;
    if (inputs.empty()) return out;
    glm::vec2 mn(1e30f), mx(-1e30f);
    for (const auto& in : inputs)
        for (const auto& c : in)
            for (const glm::vec3& p : c.pts) { mn = glm::min(mn, glm::vec2(p)); mx = glm::max(mx, glm::vec2(p)); }
    if (mn.x > mx.x) return out;
    const glm::vec2 size = glm::max(mx - mn, glm::vec2(1e-6f));
    const float cell = std::max(size.x, size.y) / res;
    const int w = (int)std::ceil(size.x / cell) + 2, h = (int)std::ceil(size.y / cell) + 2;
    const glm::vec2 org = mn - glm::vec2(cell);
    std::vector<std::vector<char>> masks;
    for (const auto& in : inputs) {
        std::vector<char> m((size_t)w * h, 0);
        // relleno par-impar por filas
        for (int y = 0; y < h; ++y) {
            const float py = org.y + (y + 0.5f) * cell;
            std::vector<float> xs;
            for (const auto& c : in) {
                if (!c.closed || c.pts.size() < 3) continue;
                for (size_t i = 0, j = c.pts.size() - 1; i < c.pts.size(); j = i++) {
                    const glm::vec2 a(c.pts[j]), b(c.pts[i]);
                    if ((a.y > py) != (b.y > py)) xs.push_back(a.x + (py - a.y) / (b.y - a.y) * (b.x - a.x));
                }
            }
            std::sort(xs.begin(), xs.end());
            for (size_t k = 0; k + 1 < xs.size(); k += 2) {
                const int x0 = std::max(0, (int)std::ceil((xs[k] - org.x) / cell - 0.5f));
                const int x1 = std::min(w - 1, (int)std::floor((xs[k + 1] - org.x) / cell - 0.5f));
                for (int x = x0; x <= x1; ++x) m[(size_t)y * w + x] = 1;
            }
        }
        masks.push_back(std::move(m));
    }
    std::vector<std::vector<char>> results;
    const size_t N = (size_t)w * h;
    auto combine = [&](auto fn) {
        std::vector<char> r(N, 0);
        for (size_t i = 0; i < N; ++i) {
            bool a = masks[0][i];
            for (size_t k = 1; k < masks.size(); ++k) a = fn(a, (bool)masks[k][i]);
            r[i] = a;
        }
        results.push_back(std::move(r));
    };
    switch (op) {
        case 0: combine([](bool a, bool b) { return a || b; }); break;
        case 1: combine([](bool a, bool b) { return a && !b; }); break;
        case 2: combine([](bool a, bool b) { return a && b; }); break;
        case 3: combine([](bool a, bool b) { return a != b; }); break;
        default:
            combine([](bool a, bool b) { return a && b; });
            combine([](bool a, bool b) { return a && !b; });
            combine([](bool a, bool b) { return !a && b; });
            break;
    }
    for (const auto& r : results) {
        for (auto& loop : marchingSquares(r, w, h)) {
            const auto simp = simplifyClosed(loop, 0.6);
            SplineContour c;
            c.closed = true;
            for (const glm::dvec2& q : simp) c.pts.push_back(glm::vec3(org.x + (float)q.x * cell + cell * 0.5f, org.y + (float)q.y * cell + cell * 0.5f, 0.0f));
            if (c.pts.size() >= 3) out.push_back(std::move(c));
        }
    }
    return out;
}

MeshData capMesh(const std::vector<SplineContour>& contours, float z, bool flip) {
    MeshData m;
    glm::vec3 u, v, n, o;
    planeBasis(contours, u, v, n, o);
    std::vector<std::vector<glm::dvec2>> cs2;
    std::vector<std::vector<glm::vec3>> cs3;
    for (const auto& c : contours) {
        if (!c.closed || c.pts.size() < 3) continue;
        std::vector<glm::dvec2> q;
        for (const glm::vec3& p : c.pts) q.push_back(glm::dvec2(glm::dot(p - o, u), glm::dot(p - o, v)));
        cs2.push_back(std::move(q));
        cs3.push_back(c.pts);
    }
    if (cs2.empty()) return m;
    std::vector<glm::dvec2> pts;
    std::vector<uint32_t> tris;
    triangulate2D(cs2, pts, tris);
    const glm::vec3 nn = flip ? -n : n;
    std::vector<glm::vec3> p3;
    for (const auto& c : cs3) p3.insert(p3.end(), c.begin(), c.end());
    for (const glm::vec3& p : p3) m.vertices.push_back(vtx(p + n * z, nn, glm::vec2(glm::dot(p, u), glm::dot(p, v))));
    for (size_t t = 0; t + 2 < tris.size(); t += 3) {
        if (flip) m.indices.insert(m.indices.end(), {tris[t], tris[t + 2], tris[t + 1]});
        else m.indices.insert(m.indices.end(), {tris[t], tris[t + 1], tris[t + 2]});
    }
    updateBounds(m);
    return m;
}

namespace {
void appendMeshTo(MeshData& dst, const MeshData& src) {
    const uint32_t base = (uint32_t)dst.vertices.size();
    dst.vertices.insert(dst.vertices.end(), src.vertices.begin(), src.vertices.end());
    for (uint32_t i : src.indices) dst.indices.push_back(base + i);
}
float signedAreaOn(const SplineContour& c, const glm::vec3& n) {
    glm::vec3 acc(0.0f);
    for (size_t i = 0, j = c.pts.size() - 1; i < c.pts.size(); j = i++) acc += glm::cross(c.pts[j], c.pts[i]);
    return glm::dot(acc, n);
}
} // namespace

MeshData extrudeContours(const std::vector<SplineContour>& contours, const glm::vec3& offset, int segments, bool caps) {
    MeshData m;
    segments = std::clamp(segments, 1, 200);
    const glm::vec3 dirN = glm::length(offset) > 1e-9f ? glm::normalize(offset) : glm::vec3(0, 0, 1);
    // paridad para saber que contornos son agujeros (en el plano perpendicular a la extrusion)
    glm::vec3 u = glm::normalize(glm::cross(std::fabs(dirN.y) < 0.9f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0), dirN)), v = glm::cross(dirN, u);
    std::vector<std::vector<glm::dvec2>> proj;
    for (const auto& c : contours) {
        std::vector<glm::dvec2> q;
        for (const glm::vec3& p : c.pts) q.push_back({glm::dot(p, u), glm::dot(p, v)});
        proj.push_back(q);
    }
    for (size_t ci = 0; ci < contours.size(); ++ci) {
        const auto& c = contours[ci];
        const size_t n = c.pts.size();
        if (n < 2) continue;
        bool hole = false;
        if (c.closed) {
            int depth = 0;
            for (size_t j = 0; j < contours.size(); ++j)
                if (j != ci && contours[j].closed && proj[j].size() >= 3 && pointInPoly(proj[ci][0], proj[j])) ++depth;
            hole = depth % 2;
        }
        std::vector<glm::vec3> P = c.pts;
        if (c.closed) {
            const float sa = signedAreaOn(c, dirN);
            if ((sa < 0) != hole) std::reverse(P.begin(), P.end());  // exterior antihorario mirando desde la extrusion
        }
        const size_t segs = c.closed ? n : n - 1;
        std::vector<glm::vec3> sn(segs);
        for (size_t i = 0; i < segs; ++i) {
            const glm::vec3 t = P[(i + 1) % n] - P[i];
            const glm::vec3 nn = glm::cross(t, offset);
            sn[i] = glm::length(nn) > 1e-12f ? glm::normalize(nn) : glm::vec3(0, 1, 0);
        }
        const float cosLim = std::cos(glm::radians(40.0f));
        auto normalAt = [&](size_t seg, bool end) {
            const size_t other = end ? seg + 1 : seg + segs - 1;
            const bool exists = c.closed || (end ? seg + 1 < segs : seg > 0);
            if (!exists) return sn[seg];
            const glm::vec3 no = sn[other % segs];
            return glm::dot(no, sn[seg]) > cosLim ? glm::normalize(no + sn[seg]) : sn[seg];
        };
        for (size_t i = 0; i < segs; ++i) {
            const glm::vec3 a = P[i], b = P[(i + 1) % n];
            const glm::vec3 na = normalAt(i, false), nb = normalAt(i, true);
            for (int s = 0; s < segments; ++s) {
                const glm::vec3 o0 = offset * ((float)s / segments), o1 = offset * ((float)(s + 1) / segments);
                const uint32_t base = (uint32_t)m.vertices.size();
                m.vertices.push_back(vtx(a + o0, na, {0, (float)s / segments}));
                m.vertices.push_back(vtx(b + o0, nb, {1, (float)s / segments}));
                m.vertices.push_back(vtx(b + o1, nb, {1, (float)(s + 1) / segments}));
                m.vertices.push_back(vtx(a + o1, na, {0, (float)(s + 1) / segments}));
                m.indices.insert(m.indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
                m.faceTris.push_back(2);
            }
        }
    }
    if (caps) {
        std::vector<SplineContour> closed;
        for (const auto& c : contours) if (c.closed && c.pts.size() >= 3) closed.push_back(c);
        if (!closed.empty()) {
            for (int side = 0; side < 2; ++side) {
                MeshData cap = capMesh(closed, 0.0f, false);
                if (cap.indices.empty()) continue;
                const glm::vec3 capN = cap.vertices[0].normal;
                const bool wantPositive = side == 1;  // tapa final mira hacia la extrusion
                const bool facesPositive = glm::dot(capN, dirN) > 0;
                if (facesPositive != wantPositive) {
                    for (size_t t = 0; t + 2 < cap.indices.size(); t += 3) std::swap(cap.indices[t + 1], cap.indices[t + 2]);
                    for (Vertex& vv : cap.vertices) vv.normal = -vv.normal;
                }
                if (side == 1)
                    for (Vertex& vv : cap.vertices) vv.position += offset;
                const size_t triBefore = m.indices.size() / 3;
                appendMeshTo(m, cap);
                const size_t added = m.indices.size() / 3 - triBefore;
                for (size_t k = 0; k < added; ++k) m.faceTris.push_back(1);
            }
        }
    }
    updateBounds(m);
    return m;
}

MeshData latheContour(const SplineContour& profile, float angleDeg, int segments) {
    MeshData m;
    const size_t n = profile.pts.size();
    if (n < 2) return m;
    segments = std::clamp(segments, 3, 512);
    const bool full = std::fabs(angleDeg) >= 359.9f;
    const int rings = full ? segments : segments + 1;
    for (int k = 0; k < rings; ++k) {
        const float a = glm::radians(angleDeg) * k / segments;
        for (size_t i = 0; i < n; ++i) {
            const glm::vec3 p = profile.pts[i];
            m.vertices.push_back(vtx(glm::vec3(p.x * std::cos(a), p.y, -p.x * std::sin(a)), glm::vec3(0, 1, 0),
                                     {(float)k / segments, (float)i / (n - 1)}));
        }
    }
    const size_t pseg = profile.closed ? n : n - 1;
    for (int k = 0; k < segments; ++k) {
        const int k1 = full ? (k + 1) % segments : k + 1;
        for (size_t i = 0; i < pseg; ++i) {
            const uint32_t a = (uint32_t)(k * n + i), b = (uint32_t)(k * n + (i + 1) % n);
            const uint32_t c = (uint32_t)(k1 * n + (i + 1) % n), d = (uint32_t)(k1 * n + i);
            m.indices.insert(m.indices.end(), {a, b, c, a, c, d});
            m.faceTris.push_back(2);
        }
    }
    smoothByPosition(m);
    // orientacion: normales hacia fuera del eje
    glm::vec3 acc(0.0f);
    for (const Vertex& v : m.vertices) acc += glm::vec3(v.position.x, 0, v.position.z) * glm::dot(v.normal, glm::normalize(glm::vec3(v.position.x, 0, v.position.z) + 1e-9f));
    float s = 0;
    for (const Vertex& v : m.vertices) {
        const glm::vec3 r(v.position.x, 0, v.position.z);
        if (glm::length(r) > 1e-6f) s += glm::dot(v.normal, glm::normalize(r));
    }
    if (s < 0) {
        for (size_t t = 0; t + 2 < m.indices.size(); t += 3) std::swap(m.indices[t + 1], m.indices[t + 2]);
        for (Vertex& v : m.vertices) v.normal = -v.normal;
    }
    updateBounds(m);
    return m;
}

float contourLength(const SplineContour& c) {
    float L = 0;
    const size_t n = c.pts.size();
    if (n < 2) return 0;
    const size_t segs = c.closed ? n : n - 1;
    for (size_t i = 0; i < segs; ++i) L += glm::length(c.pts[(i + 1) % n] - c.pts[i]);
    return L;
}

glm::vec3 contourPointAt(const SplineContour& c, float t, glm::vec3* tangent) {
    const size_t n = c.pts.size();
    if (n == 0) return glm::vec3(0.0f);
    if (n == 1) { if (tangent) *tangent = glm::vec3(1, 0, 0); return c.pts[0]; }
    const float L = contourLength(c);
    float target = std::clamp(t, 0.0f, 1.0f) * L;
    const size_t segs = c.closed ? n : n - 1;
    for (size_t i = 0; i < segs; ++i) {
        const glm::vec3 a = c.pts[i], b = c.pts[(i + 1) % n];
        const float l = glm::length(b - a);
        if (target <= l || i + 1 == segs) {
            if (tangent) *tangent = l > 1e-12f ? (b - a) / l : glm::vec3(1, 0, 0);
            return l > 1e-12f ? a + (b - a) * std::min(1.0f, target / l) : a;
        }
        target -= l;
    }
    return c.pts.back();
}

SplineContour resampleContour(const SplineContour& c, int count) {
    SplineContour r;
    r.closed = c.closed;
    count = std::max(count, 2);
    for (int i = 0; i < count; ++i) {
        const float t = c.closed ? (float)i / count : (float)i / (count - 1);
        r.pts.push_back(contourPointAt(c, t));
    }
    return r;
}

SplineContour contourPart(const SplineContour& c, float t0, float t1) {
    SplineContour r;
    r.closed = false;
    if (c.pts.size() < 2) return r;
    t0 = std::clamp(t0, 0.0f, 1.0f);
    t1 = std::clamp(t1, t0, 1.0f);
    const int n = std::max(2, (int)std::ceil((float)(c.pts.size() + 1) * (t1 - t0)) + 2);
    for (int i = 0; i < n; ++i) r.pts.push_back(contourPointAt(c, t0 + (t1 - t0) * i / (n - 1)));
    return r;
}

MeshData loftContours(const std::vector<SplineContour>& ringsIn, int segmentsU, bool caps) {
    MeshData m;
    if (ringsIn.size() < 2) return m;
    size_t M = 32;
    bool closed = true;
    for (const auto& r : ringsIn) { M = std::max(M, r.pts.size()); closed = closed && r.closed; }
    M = std::min<size_t>(M, 256);
    std::vector<SplineContour> rings;
    for (const auto& r : ringsIn) rings.push_back(resampleContour(r, (int)M));
    for (size_t k = 1; k < rings.size() && closed; ++k) {  // alinea el punto de inicio con el anillo anterior
        size_t best = 0;
        float bd = 1e30f;
        for (size_t s = 0; s < M; ++s) {
            float d = 0;
            for (size_t i = 0; i < M; i += std::max<size_t>(1, M / 16)) d += glm::length(rings[k].pts[(i + s) % M] - rings[k - 1].pts[i]);
            if (d < bd) { bd = d; best = s; }
        }
        std::rotate(rings[k].pts.begin(), rings[k].pts.begin() + (long)best, rings[k].pts.end());
    }
    segmentsU = std::clamp(segmentsU, 1, 64);
    const int R = (int)rings.size();
    auto at = [&](int k, size_t i) { return rings[(size_t)std::clamp(k, 0, R - 1)].pts[i]; };
    const int rows = (R - 1) * segmentsU + 1;
    for (int row = 0; row < rows; ++row) {
        const int k = std::min(row / segmentsU, R - 2);
        const float t = (float)(row - k * segmentsU) / segmentsU, t2 = t * t, t3 = t2 * t;
        for (size_t i = 0; i < M; ++i) {
            const glm::vec3 p0 = at(k - 1, i), p1 = at(k, i), p2 = at(k + 1, i), p3 = at(k + 2, i);
            const glm::vec3 p = 0.5f * ((2.0f * p1) + (-p0 + p2) * t + (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2 + (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3);
            m.vertices.push_back(vtx(p, glm::vec3(0, 1, 0), {(float)i / M, (float)row / (rows - 1)}));
        }
    }
    const size_t segs = closed ? M : M - 1;
    for (int row = 0; row + 1 < rows; ++row)
        for (size_t i = 0; i < segs; ++i) {
            const uint32_t a = (uint32_t)(row * M + i), b = (uint32_t)(row * M + (i + 1) % M);
            const uint32_t c = (uint32_t)((row + 1) * M + (i + 1) % M), d = (uint32_t)((row + 1) * M + i);
            m.indices.insert(m.indices.end(), {a, b, c, a, c, d});
            m.faceTris.push_back(2);
        }
    smoothByPosition(m);
    if (caps && closed) {
        for (int side = 0; side < 2; ++side) {
            const SplineContour& rr = side == 0 ? rings.front() : rings.back();
            MeshData cap = capMesh({rr}, 0.0f, false);
            if (cap.indices.empty()) continue;
            const glm::vec3 inward = (side == 0 ? rings[1].pts[0] : rings[(size_t)R - 2].pts[0]) - rr.pts[0];
            if (glm::dot(cap.vertices[0].normal, inward) > 0) {
                for (size_t t = 0; t + 2 < cap.indices.size(); t += 3) std::swap(cap.indices[t + 1], cap.indices[t + 2]);
                for (Vertex& vv : cap.vertices) vv.normal = -vv.normal;
            }
            const size_t before = m.indices.size() / 3;
            appendMeshTo(m, cap);
            for (size_t k2 = before; k2 < m.indices.size() / 3; ++k2) m.faceTris.push_back(1);
        }
    }
    updateBounds(m);
    return m;
}

MeshData sweepContour(const SplineContour& profile, const SplineContour& path, float endScale, float rotateDeg, bool caps) {
    MeshData m;
    if (profile.pts.size() < 2 || path.pts.size() < 2) return m;
    const int K = std::clamp((int)path.pts.size(), 2, 400);
    const SplineContour pth = path.pts.size() > 400 ? resampleContour(path, 400) : path;
    const size_t P = profile.pts.size();
    std::vector<glm::vec3> T(K), N(K), B(K), O(K);
    for (int i = 0; i < K; ++i) {
        O[i] = pth.pts[(size_t)i];
        const glm::vec3 a = pth.pts[(size_t)std::max(0, i - 1)], b = pth.pts[(size_t)std::min(K - 1, i + 1)];
        T[i] = glm::length(b - a) > 1e-12f ? glm::normalize(b - a) : glm::vec3(0, 0, 1);
    }
    N[0] = glm::normalize(glm::cross(T[0], std::fabs(T[0].y) < 0.95f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0)));
    for (int i = 1; i < K; ++i) {
        glm::vec3 n = N[i - 1] - glm::dot(N[i - 1], T[i]) * T[i];
        N[i] = glm::length(n) > 1e-9f ? glm::normalize(n) : N[i - 1];
    }
    for (int i = 0; i < K; ++i) B[i] = glm::cross(T[i], N[i]);
    for (int i = 0; i < K; ++i) {
        const float t = (float)i / (K - 1);
        const float sc = 1.0f + (endScale - 1.0f) * t, rot = glm::radians(rotateDeg) * t;
        const float cr = std::cos(rot), sr = std::sin(rot);
        for (size_t j = 0; j < P; ++j) {
            const glm::vec3 q = profile.pts[j];
            const float x = (q.x * cr - q.y * sr) * sc, y = (q.x * sr + q.y * cr) * sc;
            m.vertices.push_back(vtx(O[i] + N[i] * x + B[i] * y, glm::vec3(0, 1, 0), {(float)j / P, t}));
        }
    }
    const size_t psegs = profile.closed ? P : P - 1;
    for (int i = 0; i + 1 < K; ++i)
        for (size_t j = 0; j < psegs; ++j) {
            const uint32_t a = (uint32_t)(i * P + j), b = (uint32_t)(i * P + (j + 1) % P);
            const uint32_t c = (uint32_t)((i + 1) * P + (j + 1) % P), d = (uint32_t)((i + 1) * P + j);
            m.indices.insert(m.indices.end(), {a, d, c, a, c, b});
            m.faceTris.push_back(2);
        }
    smoothByPosition(m);
    if (caps && profile.closed && !path.closed) {
        for (int side = 0; side < 2; ++side) {
            SplineContour r;
            r.closed = true;
            const int i = side == 0 ? 0 : K - 1;
            for (size_t j = 0; j < P; ++j) r.pts.push_back(m.vertices[(size_t)i * P + j].position);
            MeshData cap = capMesh({r}, 0.0f, false);
            if (cap.indices.empty()) continue;
            const glm::vec3 outward = side == 0 ? -T[0] : T[(size_t)K - 1];
            if (glm::dot(cap.vertices[0].normal, outward) < 0) {
                for (size_t t = 0; t + 2 < cap.indices.size(); t += 3) std::swap(cap.indices[t + 1], cap.indices[t + 2]);
                for (Vertex& vv : cap.vertices) vv.normal = -vv.normal;
            }
            const size_t before = m.indices.size() / 3;
            appendMeshTo(m, cap);
            for (size_t k2 = before; k2 < m.indices.size() / 3; ++k2) m.faceTris.push_back(1);
        }
    }
    // orientacion: que las normales de los lados salgan hacia fuera del recorrido
    float s = 0;
    for (int i = 0; i < K; ++i)
        for (size_t j = 0; j < P; ++j) s += glm::dot(m.vertices[(size_t)i * P + j].normal, m.vertices[(size_t)i * P + j].position - O[i]);
    if (s < 0) {
        for (size_t t = 0; t + 2 < m.indices.size(); t += 3) std::swap(m.indices[t + 1], m.indices[t + 2]);
        for (Vertex& v : m.vertices) v.normal = -v.normal;
    }
    updateBounds(m);
    return m;
}

std::vector<SplineContour> vectorizeImage(const std::string& path, float threshold, float width) {
    std::vector<SplineContour> out;
    if (path.empty()) return out;
    std::ifstream in(path, std::ios::binary);
    if (!in) return out;
    std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    int w = 0, h = 0, ch = 0;
    unsigned char* px = stbi_load_from_memory(bytes.data(), (int)bytes.size(), &w, &h, &ch, 1);
    if (!px) return out;
    const int maxSide = 256;
    const int step = std::max(1, std::max(w, h) / maxSide);
    const int gw = w / step, gh = h / step;
    std::vector<char> mask((size_t)gw * gh, 0);
    for (int y = 0; y < gh; ++y)
        for (int x = 0; x < gw; ++x)
            mask[(size_t)y * gw + x] = px[(size_t)(y * step) * w + x * step] < threshold * 255.0f ? 1 : 0;  // oscuro = dentro
    stbi_image_free(px);
    const float sc = (width / kCm) / std::max(1, gw);
    for (auto& loop : marchingSquares(mask, gw, gh)) {
        const auto simp = simplifyClosed(loop, 0.7);
        SplineContour c;
        c.closed = true;
        for (const glm::dvec2& q : simp) c.pts.push_back(glm::vec3(((float)q.x - gw * 0.5f) * sc, (gh * 0.5f - (float)q.y) * sc, 0.0f));
        if (c.pts.size() >= 3) out.push_back(std::move(c));
    }
    return out;
}

} // namespace iron3d
