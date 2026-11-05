#include "ThumbnailRenderer.h"

#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

namespace iron3d {

namespace {

struct Tri {
    glm::vec3 p[3]; // mundo
    glm::vec3 n[3]; // normales en mundo
    glm::vec3 color;
    float metallic, roughness;
};

const MeshData& primitiveMesh(PrimitiveType type) {
    static const MeshData cube = makeCubeMesh();
    static const MeshData sphere = makeSphereMesh(24, 36);
    static const MeshData plane = makePlaneMesh();
    static const MeshData empty;
    switch (type) {
        case PrimitiveType::Cube: return cube;
        case PrimitiveType::Sphere: return sphere;
        case PrimitiveType::Plane: return plane;
        default: return empty;
    }
}

void collect(const SceneNode& node, const glm::mat4& parentWorld, bool parentVisible,
             const std::vector<Material>& materials, std::vector<Tri>& out) {
    const glm::mat4 world = parentWorld * node.localMatrix();
    const bool visible = resolveVisibility(node.editorVis, parentVisible);
    if (visible) {
        const MeshData* mesh = nullptr;
        if (node.primitive == PrimitiveType::Mesh) mesh = node.mesh.get();
        else mesh = &primitiveMesh(node.primitive);
        if (mesh && !mesh->indices.empty()) {
            const Material& m = materialOrDefault(materials, node.materialIndex);
            const glm::mat3 nm = glm::inverseTranspose(glm::mat3(world));
            const size_t nv = mesh->vertices.size();
            for (size_t i = 0; i + 2 < mesh->indices.size(); i += 3) {
                Tri t{};
                bool ok = true;
                for (int k = 0; k < 3; ++k) {
                    const uint32_t idx = mesh->indices[i + (size_t)k];
                    if (idx >= nv) { ok = false; break; }
                    t.p[k] = glm::vec3(world * glm::vec4(mesh->vertices[idx].position, 1.0f));
                    t.n[k] = nm * mesh->vertices[idx].normal;
                }
                if (!ok) continue;
                t.color = m.baseColor;
                t.metallic = m.metallic;
                t.roughness = m.roughness;
                out.push_back(t);
            }
        }
    }
    for (const auto& c : node.children()) collect(*c, world, visible, materials, out);
}

float toSrgb(float v) {
    v = std::clamp(v, 0.0f, 1.0f);
    return v <= 0.0031308f ? v * 12.92f : 1.055f * std::pow(v, 1.0f / 2.4f) - 0.055f;
}

} // namespace

Thumbnail renderThumbnail(const std::vector<const SceneNode*>& roots, const std::vector<Material>& materials, int size) {
    size = std::clamp(size, 16, 1024);
    constexpr int kSS = 2; // antialiasing 2x2
    const int W = size * kSS;

    std::vector<Tri> tris;
    for (const SceneNode* r : roots)
        if (r) collect(*r, glm::mat4(1.0f), true, materials, tris);

    // Fondo: degradado gris oscuro, como las miniaturas de C4D.
    std::vector<glm::vec3> color((size_t)W * (size_t)W);
    for (int y = 0; y < W; ++y) {
        const float t = (float)y / (float)(W - 1);
        const glm::vec3 bg = glm::mix(glm::vec3(0.115f), glm::vec3(0.045f), t); // lineal
        std::fill(color.begin() + (ptrdiff_t)y * W, color.begin() + (ptrdiff_t)(y + 1) * W, bg);
    }
    std::vector<float> depth((size_t)W * (size_t)W, std::numeric_limits<float>::infinity());

    Thumbnail out;
    if (!tris.empty()) {
        glm::vec3 lo(std::numeric_limits<float>::max()), hi(-std::numeric_limits<float>::max());
        for (const Tri& t : tris)
            for (const glm::vec3& p : t.p) { lo = glm::min(lo, p); hi = glm::max(hi, p); }
        const glm::vec3 center = (lo + hi) * 0.5f;
        float radius = 0.0f;
        for (const Tri& t : tris)
            for (const glm::vec3& p : t.p) radius = std::max(radius, glm::length(p - center));
        radius = std::max(radius, 1e-4f);

        // Desde delante a la derecha y un poco por encima.
        const float fov = glm::radians(30.0f);
        const glm::vec3 dir = glm::normalize(glm::vec3(0.55f, 0.42f, 1.0f));
        const float dist = radius / std::sin(fov * 0.5f) * 1.04f;
        const glm::vec3 eye = center + dir * dist;
        const glm::mat4 view = glm::lookAt(eye, center, glm::vec3(0.0f, 1.0f, 0.0f));
        const float nearZ = std::max(dist - radius * 1.5f, dist * 0.01f), farZ = dist + radius * 1.5f;
        const glm::mat4 proj = glm::perspective(fov, 1.0f, nearZ, farZ);
        glm::mat4 vp = proj * view;

        // Encuadre ajustado: la esfera envolvente deja mucho margen en
        // modelos alargados, asi que se mide lo que ocupa en pantalla y se
        // amplia/centra en 2D hasta llenar el 88 %.
        {
            glm::vec2 slo(std::numeric_limits<float>::max()), shi(-std::numeric_limits<float>::max());
            for (const Tri& t : tris)
                for (const glm::vec3& p : t.p) {
                    const glm::vec4 c = vp * glm::vec4(p, 1.0f);
                    if (c.w <= 1e-6f) continue;
                    const glm::vec2 ndc = glm::vec2(c) / c.w;
                    slo = glm::min(slo, ndc);
                    shi = glm::max(shi, ndc);
                }
            const glm::vec2 ext = shi - slo;
            const float big = std::max(ext.x, ext.y);
            if (big > 1e-6f) {
                const float k = 1.76f / big;
                const glm::vec2 mid = (slo + shi) * 0.5f;
                // ndc' = (ndc - mid) * k, en clip: x' = k*x - mid.x*k*w (la
                // columna 3 de la matriz es la que multiplica a w).
                glm::mat4 fit(1.0f);
                fit[0][0] = k;
                fit[1][1] = k;
                fit[3][0] = -mid.x * k;
                fit[3][1] = -mid.y * k;
                vp = fit * vp;
            }
        }

        // Luces en espacio de mundo, relativas a la camara.
        const glm::vec3 camRight = glm::normalize(glm::cross(-dir, glm::vec3(0.0f, 1.0f, 0.0f)));
        const glm::vec3 camUp = glm::cross(camRight, -dir);
        const glm::vec3 key = glm::normalize(dir * 0.6f - camRight * 0.7f + camUp * 0.8f);
        const glm::vec3 fill = glm::normalize(dir * 0.5f + camRight * 0.9f - camUp * 0.2f);

        for (const Tri& t : tris) {
            glm::vec4 c[3];
            bool behind = false;
            for (int k = 0; k < 3; ++k) {
                c[k] = vp * glm::vec4(t.p[k], 1.0f);
                if (c[k].w <= 1e-6f) behind = true;
            }
            if (behind) continue;
            glm::vec3 s[3];
            for (int k = 0; k < 3; ++k) {
                const glm::vec3 ndc = glm::vec3(c[k]) / c[k].w;
                s[k] = glm::vec3((ndc.x * 0.5f + 0.5f) * (float)W, (0.5f - ndc.y * 0.5f) * (float)W, ndc.z);
            }
            const float area = (s[1].x - s[0].x) * (s[2].y - s[0].y) - (s[2].x - s[0].x) * (s[1].y - s[0].y);
            if (std::fabs(area) < 1e-9f) continue;
            const int x0 = std::max(0, (int)std::floor(std::min({s[0].x, s[1].x, s[2].x})));
            const int x1 = std::min(W - 1, (int)std::ceil(std::max({s[0].x, s[1].x, s[2].x})));
            const int y0 = std::max(0, (int)std::floor(std::min({s[0].y, s[1].y, s[2].y})));
            const int y1 = std::min(W - 1, (int)std::ceil(std::max({s[0].y, s[1].y, s[2].y})));
            if (x0 > x1 || y0 > y1) continue;
            const glm::vec3 faceN = glm::normalize(glm::cross(t.p[1] - t.p[0], t.p[2] - t.p[0]));
            ++out.triangles;
            for (int y = y0; y <= y1; ++y) {
                for (int x = x0; x <= x1; ++x) {
                    const float px = (float)x + 0.5f, py = (float)y + 0.5f;
                    float w0 = (s[1].x - px) * (s[2].y - py) - (s[2].x - px) * (s[1].y - py);
                    float w1 = (s[2].x - px) * (s[0].y - py) - (s[0].x - px) * (s[2].y - py);
                    float w2 = (s[0].x - px) * (s[1].y - py) - (s[1].x - px) * (s[0].y - py);
                    if (area < 0.0f) { w0 = -w0; w1 = -w1; w2 = -w2; }
                    if (w0 < 0.0f || w1 < 0.0f || w2 < 0.0f) continue;
                    const float inv = 1.0f / std::fabs(area);
                    w0 *= inv; w1 *= inv; w2 *= inv;
                    const float z = w0 * s[0].z + w1 * s[1].z + w2 * s[2].z;
                    const size_t i = (size_t)y * (size_t)W + (size_t)x;
                    if (z < -1.0f || z > 1.0f || z >= depth[i]) continue;
                    depth[i] = z;

                    glm::vec3 n = w0 * t.n[0] + w1 * t.n[1] + w2 * t.n[2];
                    const float nl = glm::length(n);
                    n = nl > 1e-12f ? n / nl : faceN;
                    const glm::vec3 p = w0 * t.p[0] + w1 * t.p[1] + w2 * t.p[2];
                    const glm::vec3 v = glm::normalize(eye - p);
                    if (glm::dot(n, v) < 0.0f) n = -n; // Iron3D dibuja las dos caras
                    const float dk = std::max(0.0f, glm::dot(n, key));
                    const float df = std::max(0.0f, glm::dot(n, fill));
                    const float shin = 4.0f + (1.0f - t.roughness) * (1.0f - t.roughness) * 120.0f;
                    const float spec = std::pow(std::max(0.0f, glm::dot(n, glm::normalize(key + v))), shin) *
                                       (1.0f - t.roughness * 0.8f);
                    const float rim = std::pow(1.0f - std::max(0.0f, glm::dot(n, v)), 3.0f) * 0.12f;
                    const glm::vec3 diffuse = t.color * (1.0f - t.metallic);
                    const glm::vec3 specColor = glm::mix(glm::vec3(0.06f), t.color, t.metallic);
                    glm::vec3 col = diffuse * (0.16f + 0.86f * dk + 0.28f * df) + specColor * (spec * 1.6f + 0.25f * t.metallic) +
                                    glm::vec3(rim);
                    color[i] = col;
                }
            }
        }
    }

    out.width = out.height = size;
    out.rgba.resize((size_t)size * (size_t)size * 4);
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            glm::vec3 sum(0.0f);
            for (int dy = 0; dy < kSS; ++dy)
                for (int dx = 0; dx < kSS; ++dx) sum += color[(size_t)(y * kSS + dy) * (size_t)W + (size_t)(x * kSS + dx)];
            sum /= (float)(kSS * kSS);
            uint8_t* px = &out.rgba[((size_t)y * (size_t)size + (size_t)x) * 4];
            px[0] = (uint8_t)std::lround(toSrgb(sum.r) * 255.0f);
            px[1] = (uint8_t)std::lround(toSrgb(sum.g) * 255.0f);
            px[2] = (uint8_t)std::lround(toSrgb(sum.b) * 255.0f);
            px[3] = 255;
        }
    }
    return out;
}

} // namespace iron3d
