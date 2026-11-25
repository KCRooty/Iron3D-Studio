#include "ThumbnailRenderer.h"
#include "ChannelBake.h"
#include "TextureImage.h"
#include "SceneLighting.h"

#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <stb_image.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <mutex>

namespace iron3d {

namespace {

// Textura de Color ya en lineal (PNG/JPEG via stb_image). Soporte minimo
// del render por CPU: solo el canal Color, muestreo bilineal, mosaico y
// desplazamiento de la etiqueta de textura (proyeccion UVW).
struct Image {
    int w = 0, h = 0;
    std::vector<glm::vec3> px;
    glm::vec3 sample(glm::vec2 uv, bool tile) const {
        if (!tile && (uv.x < 0.0f || uv.x > 1.0f || uv.y < 0.0f || uv.y > 1.0f)) return glm::vec3(-1.0f);
        const float fx = uv.x * (float)w - 0.5f, fy = uv.y * (float)h - 0.5f;
        const float x0 = std::floor(fx), y0 = std::floor(fy);
        const float ax = fx - x0, ay = fy - y0;
        auto at = [&](float x, float y) {
            int ix = (int)x, iy = (int)y;
            ix = ((ix % w) + w) % w;
            iy = ((iy % h) + h) % h;
            return px[(size_t)iy * (size_t)w + (size_t)ix];
        };
        return glm::mix(glm::mix(at(x0, y0), at(x0 + 1, y0), ax), glm::mix(at(x0, y0 + 1), at(x0 + 1, y0 + 1), ax), ay);
    }
};

float srgbToLinear(float v) { return v <= 0.04045f ? v / 12.92f : std::pow((v + 0.055f) / 1.055f, 2.4f); }

// Cache por ruta (las miniaturas se renderizan muchas veces con los mismos
// materiales). nullptr = no existe o no se puede leer.
std::shared_ptr<const Image> loadImage(const std::string& path) {
    static std::mutex mtx;
    static std::map<std::string, std::shared_ptr<const Image>> cache;
    std::lock_guard<std::mutex> lock(mtx);
    auto it = cache.find(path);
    if (it != cache.end()) return it->second;
    std::shared_ptr<const Image> result;
    std::ifstream f(std::filesystem::path(std::u8string(path.begin(), path.end())), std::ios::binary);
    if (f) {
        const std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        int w = 0, h = 0, comp = 0;
        if (stbi_uc* data = bytes.empty() ? nullptr : stbi_load_from_memory(bytes.data(), (int)bytes.size(), &w, &h, &comp, 3)) {
            auto img = std::make_shared<Image>();
            img->w = w;
            img->h = h;
            img->px.resize((size_t)w * (size_t)h);
            for (size_t i = 0; i < img->px.size(); ++i)
                img->px[i] = glm::vec3(srgbToLinear(data[i * 3] / 255.0f), srgbToLinear(data[i * 3 + 1] / 255.0f),
                                       srgbToLinear(data[i * 3 + 2] / 255.0f));
            stbi_image_free(data);
            result = img;
        }
    }
    cache.emplace(path, result);
    return result;
}

// Imagen final del canal Color (con su arbol de shaders horneado: Filtro, Capa, Colorizador...), la misma
// que ve el visor. Cache por clave del canal (2026-11-25): sin esto los trajes de .lib4d salian en blanco.
std::shared_ptr<const Image> loadChannelImage(const Material& m) {
    static std::mutex mtx;
    static std::map<std::string, std::shared_ptr<const Image>> cache;
    const std::string key = channelTextureKey(m, TexSlot::Color);
    if (key.empty()) return nullptr;
    {
        std::lock_guard<std::mutex> lock(mtx);
        auto it = cache.find(key);
        if (it != cache.end()) return it->second;
    }
    std::shared_ptr<const Image> result;
    TextureImage ti;
    if (loadChannelTexture(m, TexSlot::Color, ti) && ti.width > 0 && ti.height > 0) {
        auto img = std::make_shared<Image>();
        img->w = ti.width;
        img->h = ti.height;
        img->px.resize((size_t)ti.width * (size_t)ti.height);
        for (size_t i = 0; i < img->px.size(); ++i)
            img->px[i] = glm::vec3(srgbToLinear(ti.rgba[i * 4] / 255.0f), srgbToLinear(ti.rgba[i * 4 + 1] / 255.0f),
                                   srgbToLinear(ti.rgba[i * 4 + 2] / 255.0f));
        result = img;
    }
    if (std::getenv("IRON3D_DBG_TEX")) {
        glm::vec3 avg(0.0f);
        if (result) for (const glm::vec3& c : result->px) avg += c;
        if (result) avg /= (float)result->px.size();
        std::fprintf(stderr, "[cpu-tex] %s -> %s avg=(%.2f %.2f %.2f)\n", m.name.c_str(), result ? "ok" : "NULL", avg.x, avg.y, avg.z);
    }
    std::lock_guard<std::mutex> lock(mtx);
    cache.emplace(key, result);
    return result;
}

struct Tri {
    glm::vec3 p[3]; // mundo
    glm::vec3 n[3]; // normales en mundo
    glm::vec2 uv[3];
    glm::vec3 color;
    glm::vec3 emission;
    float metallic, roughness;
    float opacity = 1.0f;            // 1 - Transparencia del material (solo cuenta con los efectos de estudio)
    const Image* tex = nullptr;      // textura de Color (vive en la cache)
    glm::vec2 texOffset{0.0f}, texLength{1.0f};
    bool texTile = true;
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
             const std::vector<Material>& materials, std::vector<Tri>& out, bool forRender = false) {
    const glm::mat4 world = parentWorld * node.localMatrix();
    const bool visible = resolveVisibility(forRender ? node.renderVis : node.editorVis, parentVisible);
    if (visible) {
        const MeshData* mesh = nullptr;
        if (node.primitive == PrimitiveType::Mesh) mesh = node.mesh.get();
        else mesh = &primitiveMesh(node.primitive);
        if (mesh && !mesh->indices.empty()) {
            const glm::mat3 nm = glm::inverseTranspose(glm::mat3(world));
            // Material y textura de un rango de la malla (las Selecciones de Poligonos de C4D traen el suyo).
            auto resolve = [&](int matIndex, const TextureTagData& tag, const Material*& mOut, const Image*& texOut) {
                mOut = &materialOrDefault(materials, matIndex);
                texOut = nullptr;
                if (tag.projection != 6 && !(tag.projection >= 0 && tag.projection <= 4)) return;
                if (auto img = loadChannelImage(*mOut)) texOut = img.get();
                else if (!mOut->texture(TexSlot::Color).empty()) {
                    const std::string path = resolveTexturePath(mOut->texture(TexSlot::Color), mOut->texBaseDir);
                    if (!path.empty())
                        if (auto img2 = loadImage(path)) texOut = img2.get();
                }
            };
            const Material* nodeM = nullptr;
            const Image* nodeTex = nullptr;
            resolve(node.materialIndex, node.texTag, nodeM, nodeTex);
            std::vector<const Material*> partM(node.materialParts.size());
            std::vector<const Image*> partTex(node.materialParts.size());
            for (size_t k = 0; k < node.materialParts.size(); ++k)
                resolve(node.materialParts[k].materialIndex, node.materialParts[k].texTag, partM[k], partTex[k]);
            const size_t nv = mesh->vertices.size();
            // Caja local de la malla: las proyecciones de C4D (Esferica, Cilindrica, Plana, Cubica, Frontal) se
            // ajustan al objeto. Aproximacion (2026-11-25): la etiqueta no trae su propia matriz.
            glm::vec3 blo(1e30f), bhi(-1e30f);
            for (const Vertex& vx : mesh->vertices) { blo = glm::min(blo, vx.position); bhi = glm::max(bhi, vx.position); }
            const glm::vec3 bc = (blo + bhi) * 0.5f, bh = glm::max((bhi - blo) * 0.5f, glm::vec3(1e-6f));
            auto project = [&](int proj, const glm::vec3& pos, const glm::vec3& nrm) {
                const glm::vec3 q = (pos - bc) / bh;  // -1..1 en la caja
                constexpr float kPi = 3.14159265f;
                switch (proj) {
                    case 0: return glm::vec2(std::atan2(q.z, q.x) / (2.0f * kPi) + 0.5f, std::acos(std::clamp(-q.y / std::max(1e-6f, glm::length(q)), -1.0f, 1.0f)) / kPi);
                    case 1: return glm::vec2(std::atan2(q.z, q.x) / (2.0f * kPi) + 0.5f, 0.5f - q.y * 0.5f);
                    case 2: return glm::vec2(q.x * 0.5f + 0.5f, 0.5f - q.y * 0.5f);
                    case 4: return glm::vec2(q.x * 0.5f + 0.5f, 0.5f - q.y * 0.5f);
                    default: {  // Cubica: el eje dominante de la normal
                        const glm::vec3 a = glm::abs(nrm);
                        if (a.z >= a.x && a.z >= a.y) return glm::vec2(q.x * 0.5f + 0.5f, 0.5f - q.y * 0.5f);
                        if (a.x >= a.y) return glm::vec2(q.z * 0.5f + 0.5f, 0.5f - q.y * 0.5f);
                        return glm::vec2(q.x * 0.5f + 0.5f, q.z * 0.5f + 0.5f);
                    }
                }
            };
            for (size_t i = 0; i + 2 < mesh->indices.size(); i += 3) {
                const Material* mp = nodeM;
                const Image* tex = nodeTex;
                const TextureTagData* tag = &node.texTag;
                if (node.primitive == PrimitiveType::Mesh)
                    for (size_t k = 0; k < node.materialParts.size(); ++k) {
                        const MaterialPart& pt = node.materialParts[k];
                        if (i >= pt.firstIndex && i < (size_t)pt.firstIndex + pt.indexCount) {
                            mp = partM[k];
                            tex = partTex[k];
                            tag = &pt.texTag;
                            break;
                        }
                    }
                const Material& m = *mp;
                Tri t{};
                bool ok = true;
                for (int k = 0; k < 3; ++k) {
                    const uint32_t idx = mesh->indices[i + (size_t)k];
                    if (idx >= nv) { ok = false; break; }
                    t.p[k] = glm::vec3(world * glm::vec4(mesh->vertices[idx].position, 1.0f));
                    t.n[k] = nm * mesh->vertices[idx].normal;
                    t.uv[k] = mesh->vertices[idx].uv;
                }
                if (!ok) continue;
                if (tex && tag->projection != 6) {
                    for (int k = 0; k < 3; ++k) {
                        const uint32_t idx = mesh->indices[i + (size_t)k];
                        t.uv[k] = project(tag->projection, mesh->vertices[idx].position, mesh->vertices[idx].normal);
                    }
                    if (tag->projection <= 1) {  // costura del giro: sube a 1 los que quedan al otro lado
                        const float lo = std::min({t.uv[0].x, t.uv[1].x, t.uv[2].x}), hi = std::max({t.uv[0].x, t.uv[1].x, t.uv[2].x});
                        if (hi - lo > 0.5f)
                            for (int k = 0; k < 3; ++k)
                                if (t.uv[k].x < 0.5f) t.uv[k].x += 1.0f;
                    }
                }
                t.color = m.baseColor;
                t.emission = m.emission;
                t.tex = tex;
                t.texOffset = tag->offset;
                t.texLength = tag->length;
                t.texTile = tag->tile;
                t.metallic = m.metallic;
                t.roughness = m.roughness;
                t.opacity = 1.0f - std::clamp((m.transparency.r + m.transparency.g + m.transparency.b) / 3.0f, 0.0f, 1.0f);
                out.push_back(t);
            }
        }
    }
    for (const auto& c : node.children()) collect(*c, world, visible, materials, out, forRender);
}

float toSrgb(float v) {
    v = std::clamp(v, 0.0f, 1.0f);
    return v <= 0.0031308f ? v * 12.92f : 1.055f * std::pow(v, 1.0f / 2.4f) - 0.055f;
}

// Lienzo supermuestreado: color lineal + profundidad.
struct Canvas {
    int w = 0, h = 0;
    std::vector<glm::vec3> color;
    std::vector<float> depth;
    Canvas(int W, int H, const glm::vec3& top, const glm::vec3& bottom) : w(W), h(H) {
        color.resize((size_t)W * (size_t)H);
        for (int y = 0; y < H; ++y) {
            const float t = H > 1 ? (float)y / (float)(H - 1) : 0.0f;
            std::fill(color.begin() + (ptrdiff_t)y * W, color.begin() + (ptrdiff_t)(y + 1) * W, glm::mix(top, bottom, t));
        }
        depth.assign((size_t)W * (size_t)H, std::numeric_limits<float>::infinity());
    }
};

struct Shading {
    glm::vec3 eye;        // perspectiva: posicion del ojo
    glm::vec3 viewDir;    // ortografica: direccion hacia la camara
    bool ortho = false;
    glm::vec3 key, fill;  // luces direccionales (hacia la luz)
    bool studio = false;  // reflejos de estudio + transparencia (RenderFx)
    glm::vec3 camRight{1, 0, 0}, camUp{0, 1, 0}, toEye{0, 0, 1};
    std::vector<ViewportLight> lights;  // luces de la escena (2026-11-25); vacio = las dos luces de camara
};

// Entorno de estudio procedural, en el sistema de la camara (x derecha, y arriba, z hacia el ojo): cielo claro
// con degradado, suelo oscuro con horizonte casi nitido (la linea que parte la esfera en el logo) y dos cajas de
// luz suaves, una grande arriba a la izquierda y otra pequena a la derecha.
glm::vec3 studioEnv(const glm::vec3& r) {
    const float y = r.y;
    const glm::vec3 sky = glm::mix(glm::vec3(0.95f, 0.97f, 1.0f), glm::vec3(1.6f, 1.65f, 1.7f), std::clamp(y, 0.0f, 1.0f));
    const glm::vec3 ground = glm::vec3(0.10f, 0.12f, 0.10f) * (1.0f + 0.8f * std::clamp(-y, 0.0f, 1.0f));
    const float h = std::clamp((y + 0.02f) / 0.04f, 0.0f, 1.0f);
    glm::vec3 c = glm::mix(ground, sky, h * h * (3.0f - 2.0f * h));
    const glm::vec3 k1 = glm::normalize(glm::vec3(-0.6f, 0.68f, 0.45f));
    const glm::vec3 k2 = glm::normalize(glm::vec3(0.78f, 0.25f, 0.35f));
    const glm::vec3 d1 = r - k1, d2 = r - k2;
    c += glm::vec3(9.0f) * std::exp(-glm::dot(d1, d1) / 0.045f);
    c += glm::vec3(1.0f) * std::exp(-glm::dot(d2, d2) / 0.03f);
    return c;
}

struct ClipVert {
    glm::vec4 c;  // espacio de recorte
    glm::vec3 p, n;
    glm::vec2 uv;
};

void shadeTriangle(Canvas& cv, const ClipVert (&v)[3], const Tri& t, const Shading& sh, bool blend = false) {
    const int W = cv.w, H = cv.h;
    glm::vec3 s[3];
    for (int k = 0; k < 3; ++k) {
        const glm::vec3 ndc = glm::vec3(v[k].c) / v[k].c.w;
        s[k] = glm::vec3((ndc.x * 0.5f + 0.5f) * (float)W, (0.5f - ndc.y * 0.5f) * (float)H, ndc.z);
    }
    const float area = (s[1].x - s[0].x) * (s[2].y - s[0].y) - (s[2].x - s[0].x) * (s[1].y - s[0].y);
    if (std::fabs(area) < 1e-9f) return;
    const int x0 = std::max(0, (int)std::floor(std::min({s[0].x, s[1].x, s[2].x})));
    const int x1 = std::min(W - 1, (int)std::ceil(std::max({s[0].x, s[1].x, s[2].x})));
    const int y0 = std::max(0, (int)std::floor(std::min({s[0].y, s[1].y, s[2].y})));
    const int y1 = std::min(H - 1, (int)std::ceil(std::max({s[0].y, s[1].y, s[2].y})));
    if (x0 > x1 || y0 > y1) return;
    const glm::vec3 fn = glm::cross(v[1].p - v[0].p, v[2].p - v[0].p);
    const glm::vec3 faceN = glm::length(fn) > 1e-20f ? glm::normalize(fn) : glm::vec3(0.0f, 1.0f, 0.0f);
    const float inv = 1.0f / std::fabs(area);
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            const float px = (float)x + 0.5f, py = (float)y + 0.5f;
            float w0 = (s[1].x - px) * (s[2].y - py) - (s[2].x - px) * (s[1].y - py);
            float w1 = (s[2].x - px) * (s[0].y - py) - (s[0].x - px) * (s[2].y - py);
            float w2 = (s[0].x - px) * (s[1].y - py) - (s[1].x - px) * (s[0].y - py);
            if (area < 0.0f) { w0 = -w0; w1 = -w1; w2 = -w2; }
            if (w0 < 0.0f || w1 < 0.0f || w2 < 0.0f) continue;
            w0 *= inv; w1 *= inv; w2 *= inv;
            const float z = w0 * s[0].z + w1 * s[1].z + w2 * s[2].z;
            const size_t i = (size_t)y * (size_t)W + (size_t)x;
            if (z < -1.0f || z > 1.0f || z >= cv.depth[i]) continue;
            if (!blend) cv.depth[i] = z;

            glm::vec3 n = w0 * v[0].n + w1 * v[1].n + w2 * v[2].n;
            const float nl = glm::length(n);
            n = nl > 1e-12f ? n / nl : faceN;
            const glm::vec3 p = w0 * v[0].p + w1 * v[1].p + w2 * v[2].p;
            const glm::vec3 vd = sh.ortho ? sh.viewDir : glm::normalize(sh.eye - p);
            const bool backSide = glm::dot(n, vd) < 0.0f;
            if (backSide) n = -n; // Iron3D dibuja las dos caras
            const float dk = std::max(0.0f, glm::dot(n, sh.key));
            const float df = std::max(0.0f, glm::dot(n, sh.fill));
            const float shin = 4.0f + (1.0f - t.roughness) * (1.0f - t.roughness) * 120.0f;
            const float spec = std::pow(std::max(0.0f, glm::dot(n, glm::normalize(sh.key + vd))), shin) *
                               (1.0f - t.roughness * 0.8f);
            const float rim = std::pow(1.0f - std::max(0.0f, glm::dot(n, vd)), 3.0f) * 0.12f;
            glm::vec3 base = t.color;
            if (t.tex) {
                // UV con correccion de perspectiva (interpoladas en 1/w).
                const float i0 = w0 / v[0].c.w, i1 = w1 / v[1].c.w, i2 = w2 / v[2].c.w;
                const glm::vec2 uv = (i0 * v[0].uv + i1 * v[1].uv + i2 * v[2].uv) / (i0 + i1 + i2);
                const glm::vec2 len = glm::max(glm::abs(t.texLength), glm::vec2(1e-6f)) * glm::sign(t.texLength + glm::vec2(1e-12f));
                const glm::vec3 texel = t.tex->sample((uv - t.texOffset) / len, t.texTile);
                if (texel.x >= 0.0f) base = texel;
            }
            const glm::vec3 diffuse = base * (1.0f - t.metallic);
            const glm::vec3 specColor = glm::mix(glm::vec3(0.06f), base, t.metallic);
            glm::vec3 lit = diffuse * (0.16f + 0.86f * dk + 0.28f * df) +
                            specColor * (spec * 1.6f + 0.25f * t.metallic) + glm::vec3(rim) + t.emission;
            if (!sh.lights.empty()) {
                // Luces de la escena, como el visor: difusa + especular de Blinn, con la caida de C4D.
                glm::vec3 dsum(0.0f), ssum(0.0f);
                for (const ViewportLight& L : sh.lights) {
                    glm::vec3 ld;
                    float att = 1.0f;
                    if (L.type == ViewportLight::Type::Directional) ld = -L.direction;
                    else {
                        const glm::vec3 d = L.position - p;
                        const float dist = glm::length(d);
                        ld = dist > 1e-6f ? d / dist : glm::vec3(0, 1, 0);
                        att = falloffFactor(L.falloff, dist, L.radius, L.innerRadius);
                        if (L.type == ViewportLight::Type::Spot) {
                            const float c = glm::dot(-ld, L.direction);
                            att *= std::clamp((c - L.cosOuter) / std::max(1e-4f, L.cosInner - L.cosOuter), 0.0f, 1.0f);
                        }
                    }
                    const float nd = std::max(0.0f, glm::dot(n, ld));
                    if (L.diffuse) dsum += L.color * (nd * att);
                    if (L.specular)
                        ssum += L.color * (std::pow(std::max(0.0f, glm::dot(n, glm::normalize(ld + vd))), shin) * att * (nd > 0.0f ? 1.0f : 0.0f));
                }
                lit = diffuse * (0.10f + dsum) + specColor * (ssum * (1.0f - t.roughness * 0.8f) * 0.8f + 0.25f * t.metallic) + t.emission;
            }
            if (!sh.studio) {
                cv.color[i] = lit;
                continue;
            }
            // Reflejo de estudio con Fresnel; el metal lo tine con su color. En la cara interior de un cristal
            // el reflejo se atenua para no duplicar las luces de la cara delantera.
            const float cosv = std::max(0.0f, glm::dot(n, vd));
            const glm::vec3 refl = glm::reflect(-vd, n);
            const glm::vec3 rc(glm::dot(refl, sh.camRight), glm::dot(refl, sh.camUp), glm::dot(refl, sh.toEye));
            const float f0 = glm::mix(t.opacity < 0.999f ? 0.05f : 0.16f, 1.0f, t.metallic);
            const float fres = f0 + (1.0f - f0) * std::pow(1.0f - cosv, 5.0f);
            const float gloss = std::pow(std::max(0.0f, 1.0f - t.roughness), 1.5f);
            glm::vec3 tint = glm::mix(glm::vec3(1.0f), base, t.metallic);
            glm::vec3 add = studioEnv(rc) * tint * (fres * gloss) * (backSide ? 0.25f : 1.0f);
            if (t.opacity < 0.999f) {
                // Cristal: lo que hay detras se ve a traves, teñido por el color del material.
                const float a = std::clamp(t.opacity, 0.0f, 1.0f);
                const glm::vec3 dst = cv.color[i];
                const glm::vec3 body = backSide ? glm::vec3(0.0f) : diffuse * (0.10f + 0.50f * dk + 0.16f * df) * a;
                cv.color[i] = dst * glm::mix(glm::vec3(1.0f), base, a) * (1.0f - a * 0.30f) + body + add +
                              specColor * (spec * 1.6f) + t.emission;
            } else {
                cv.color[i] = lit + add;
            }
        }
    }
}

// Recorta cada triangulo contra el plano cercano (w > eps) en espacio de
// recorte (Sutherland-Hodgman): un suelo que pasa por detras de la camara
// se ve hasta el borde en vez de desaparecer entero.
int rasterize(Canvas& cv, const std::vector<Tri>& tris, const glm::mat4& vp, const Shading& sh) {
    constexpr float kEps = 1e-4f;
    int drawn = 0;
    // Con los efectos de estudio primero se dibuja lo opaco y luego lo transparente, de lejos a cerca.
    std::vector<const Tri*> order, glass;
    order.reserve(tris.size());
    for (const Tri& t : tris) (sh.studio && t.opacity < 0.999f ? glass : order).push_back(&t);
    std::sort(glass.begin(), glass.end(), [&](const Tri* a, const Tri* b) {
        const glm::vec3 ca = (a->p[0] + a->p[1] + a->p[2]) / 3.0f, cb = (b->p[0] + b->p[1] + b->p[2]) / 3.0f;
        return glm::dot(ca - sh.eye, ca - sh.eye) > glm::dot(cb - sh.eye, cb - sh.eye);
    });
    const size_t nOpaque = order.size();
    order.insert(order.end(), glass.begin(), glass.end());
    size_t idxTri = 0;
    for (const Tri* tp : order) {
        const Tri& t = *tp;
        const bool blendPass = idxTri++ >= nOpaque;
        ClipVert in[3];
        int behind = 0;
        for (int k = 0; k < 3; ++k) {
            in[k] = {vp * glm::vec4(t.p[k], 1.0f), t.p[k], t.n[k], t.uv[k]};
            if (in[k].c.w <= kEps) ++behind;
        }
        if (behind == 3) continue;
        ClipVert poly[4];
        int np = 0;
        if (behind == 0) {
            poly[0] = in[0]; poly[1] = in[1]; poly[2] = in[2];
            np = 3;
        } else {
            for (int k = 0; k < 3; ++k) {
                const ClipVert& a = in[k];
                const ClipVert& b = in[(k + 1) % 3];
                const bool aIn = a.c.w > kEps, bIn = b.c.w > kEps;
                if (aIn) poly[np++] = a;
                if (aIn != bIn) {
                    const float f = (kEps - a.c.w) / (b.c.w - a.c.w);
                    poly[np++] = {glm::mix(a.c, b.c, f), glm::mix(a.p, b.p, f), glm::mix(a.n, b.n, f), glm::mix(a.uv, b.uv, f)};
                }
            }
        }
        ++drawn;
        for (int k = 1; k + 1 < np; ++k) {
            const ClipVert tri[3] = {poly[0], poly[k], poly[k + 1]};
            shadeTriangle(cv, tri, t, sh, blendPass);
        }
    }
    return drawn;
}

// Promedia el supermuestreo y pasa a sRGB de 8 bits.
void resolve(const Canvas& cv, int ss, Thumbnail& out) {
    out.width = cv.w / ss;
    out.height = cv.h / ss;
    out.rgba.resize((size_t)out.width * (size_t)out.height * 4);
    for (int y = 0; y < out.height; ++y) {
        for (int x = 0; x < out.width; ++x) {
            glm::vec3 sum(0.0f);
            for (int dy = 0; dy < ss; ++dy)
                for (int dx = 0; dx < ss; ++dx)
                    sum += cv.color[(size_t)(y * ss + dy) * (size_t)cv.w + (size_t)(x * ss + dx)];
            sum /= (float)(ss * ss);
            uint8_t* px = &out.rgba[((size_t)y * (size_t)out.width + (size_t)x) * 4];
            px[0] = (uint8_t)std::lround(toSrgb(sum.r) * 255.0f);
            px[1] = (uint8_t)std::lround(toSrgb(sum.g) * 255.0f);
            px[2] = (uint8_t)std::lround(toSrgb(sum.b) * 255.0f);
            px[3] = 255;
        }
    }
}

// Luz principal arriba a la izquierda y relleno a la derecha, relativas a la camara.
void cameraLights(const glm::vec3& toCam, const glm::vec3& camRightIn, Shading& sh) {
    glm::vec3 camRight = camRightIn;
    if (glm::length(camRight) < 1e-6f) camRight = glm::vec3(1.0f, 0.0f, 0.0f);
    camRight = glm::normalize(camRight);
    const glm::vec3 camUp = glm::normalize(glm::cross(camRight, -toCam));
    sh.key = glm::normalize(toCam * 0.6f - camRight * 0.7f + camUp * 0.8f);
    sh.fill = glm::normalize(toCam * 0.5f + camRight * 0.9f - camUp * 0.2f);
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
    Canvas cv(W, W, glm::vec3(0.115f), glm::vec3(0.045f));

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

        Shading sh;
        sh.eye = eye;
        cameraLights(dir, glm::cross(-dir, glm::vec3(0.0f, 1.0f, 0.0f)), sh);
        out.triangles = rasterize(cv, tris, vp, sh);
    }
    resolve(cv, kSS, out);
    return out;
}

Thumbnail renderCameraImage(const SceneNode& root, const std::vector<Material>& materials, const glm::mat4& view,
                            const glm::mat4& proj, int width, int height, int supersample,
                            const glm::vec3& bgTop, const glm::vec3& bgBottom) {
    return renderCameraImage(root, materials, view, proj, width, height, supersample, bgTop, bgBottom, RenderFx{});
}

Thumbnail renderCameraImage(const SceneNode& root, const std::vector<Material>& materials, const glm::mat4& view,
                            const glm::mat4& proj, int width, int height, int supersample,
                            const glm::vec3& bgTop, const glm::vec3& bgBottom, const RenderFx& fx) {
    width = std::clamp(width, 16, 8192);
    height = std::clamp(height, 16, 8192);
    supersample = std::clamp(supersample, 1, 4);
    std::vector<Tri> tris;
    for (const auto& c : root.children()) collect(*c, glm::mat4(1.0f), true, materials, tris, true);

    Canvas cv(width * supersample, height * supersample, bgTop, bgBottom);
    const glm::mat4 inv = glm::inverse(view);
    Shading sh;
    sh.eye = glm::vec3(inv[3]);
    sh.ortho = std::fabs(proj[3][3] - 1.0f) < 1e-6f;
    sh.viewDir = glm::normalize(glm::vec3(inv[2]));  // +Z de la camara = hacia el ojo
    cameraLights(sh.viewDir, glm::vec3(inv[0]), sh);
    sh.lights = collectSceneLights(root).lights;
    sh.studio = fx.studio;
    sh.camRight = glm::normalize(glm::vec3(inv[0]));
    sh.camUp = glm::normalize(glm::vec3(inv[1]));
    sh.toEye = sh.viewDir;
    const glm::mat4 vp = proj * view;
    if (fx.shadow && !tris.empty()) {
        // Sombra suave en el suelo: una elipse difusa bajo la caja de contorno de la escena, en pantalla.
        glm::vec3 lo(1e30f), hi(-1e30f);
        for (const Tri& t : tris)
            for (int k = 0; k < 3; ++k) { lo = glm::min(lo, t.p[k]); hi = glm::max(hi, t.p[k]); }
        float minX = 1e30f, maxX = -1e30f, maxY = -1e30f;
        for (int c = 0; c < 8; ++c) {
            const glm::vec4 q = vp * glm::vec4((c & 1) ? hi.x : lo.x, (c & 2) ? hi.y : lo.y, (c & 4) ? hi.z : lo.z, 1.0f);
            if (q.w <= 1e-4f) continue;
            const float sx = (q.x / q.w * 0.5f + 0.5f) * (float)cv.w, sy = (0.5f - q.y / q.w * 0.5f) * (float)cv.h;
            minX = std::min(minX, sx); maxX = std::max(maxX, sx); maxY = std::max(maxY, sy);
        }
        if (maxX > minX) {
            const float cx = (minX + maxX) * 0.5f, bw = maxX - minX;
            const float cy = maxY + bw * 0.04f, sxs = bw * 0.34f, sys = bw * 0.042f;
            const int y0 = std::max(0, (int)(cy - sys * 3.2f)), y1 = std::min(cv.h - 1, (int)(cy + sys * 3.2f));
            const int x0 = std::max(0, (int)(cx - sxs * 3.2f)), x1 = std::min(cv.w - 1, (int)(cx + sxs * 3.2f));
            for (int y = y0; y <= y1; ++y)
                for (int x = x0; x <= x1; ++x) {
                    const float dx = ((float)x - cx) / sxs, dy = ((float)y - cy) / sys;
                    const float a = 0.62f * std::exp(-0.5f * (dx * dx + dy * dy));
                    glm::vec3& px = cv.color[(size_t)y * (size_t)cv.w + (size_t)x];
                    px *= glm::vec3(1.0f - a, 1.0f - a * 0.97f, 1.0f - a * 0.9f);
                }
        }
    }
    Thumbnail out;
    out.triangles = rasterize(cv, tris, vp, sh);
    resolve(cv, supersample, out);
    return out;
}

} // namespace iron3d
