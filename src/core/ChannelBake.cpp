#include "ChannelBake.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <sstream>

namespace iron3d {

namespace {

using glm::vec3;
using glm::vec4;

bool colorMixChannel(TexSlot s) { return s == TexSlot::Color || s == TexSlot::Emission || s == TexSlot::Transparency; }

// --- Color ----------------------------------------------------------------------

float luminance(const vec3& c) { return 0.299f * c.r + 0.587f * c.g + 0.114f * c.b; }

vec3 rgbToHsl(const vec3& c) {
    const float mx = std::max({c.r, c.g, c.b}), mn = std::min({c.r, c.g, c.b});
    const float l = 0.5f * (mx + mn);
    if (mx - mn < 1e-6f) return vec3(0.0f, 0.0f, l);
    const float d = mx - mn;
    const float s = l > 0.5f ? d / (2.0f - mx - mn) : d / (mx + mn);
    float h;
    if (mx == c.r) h = (c.g - c.b) / d + (c.g < c.b ? 6.0f : 0.0f);
    else if (mx == c.g) h = (c.b - c.r) / d + 2.0f;
    else h = (c.r - c.g) / d + 4.0f;
    return vec3(h / 6.0f, s, l);
}

float hue2rgb(float p, float q, float t) {
    if (t < 0.0f) t += 1.0f;
    if (t > 1.0f) t -= 1.0f;
    if (t < 1.0f / 6.0f) return p + (q - p) * 6.0f * t;
    if (t < 0.5f) return q;
    if (t < 2.0f / 3.0f) return p + (q - p) * (2.0f / 3.0f - t) * 6.0f;
    return p;
}

vec3 hslToRgb(const vec3& hsl) {
    const float h = hsl.x - std::floor(hsl.x), s = hsl.y, l = hsl.z;
    if (s <= 1e-6f) return vec3(l);
    const float q = l < 0.5f ? l * (1.0f + s) : l + s - l * s;
    const float p = 2.0f * l - q;
    return vec3(hue2rgb(p, q, h + 1.0f / 3.0f), hue2rgb(p, q, h), hue2rgb(p, q, h - 1.0f / 3.0f));
}

// Tono (grados), saturacion y luminosidad (-1..1), como Filtro y la capa HSL.
vec3 adjustHsl(const vec3& c, float hueDeg, float sat, float light, bool colorize) {
    vec3 hsl = rgbToHsl(glm::clamp(c, 0.0f, 1.0f));
    if (colorize) {
        hsl.x = hueDeg / 360.0f;
        hsl.y = std::clamp(0.5f + 0.5f * sat, 0.0f, 1.0f);
    } else {
        hsl.x += hueDeg / 360.0f;
        hsl.y = std::clamp(hsl.y * (1.0f + sat), 0.0f, 1.0f);
    }
    if (light > 0.0f) hsl.z = hsl.z + (1.0f - hsl.z) * light;
    else hsl.z = hsl.z * (1.0f + light);
    return hslToRgb(hsl);
}

vec3 brightContrastGamma(vec3 c, float bright, float contrast, float gamma) {
    c += vec3(bright);
    c = (c - vec3(0.5f)) * (1.0f + contrast) + vec3(0.5f);
    c = glm::max(c, vec3(0.0f));
    if (gamma > 1e-4f && std::fabs(gamma - 1.0f) > 1e-4f) c = glm::pow(c, vec3(1.0f / gamma));
    return c;
}

float channelInput(const vec3& c, int which) { // 0 lum, 1 tono, 2 sat, 3 luminosidad, 4 r, 5 g, 6 b
    switch (which) {
        case 1: return rgbToHsl(c).x;
        case 2: return rgbToHsl(c).y;
        case 3: return rgbToHsl(c).z;
        case 4: return c.r;
        case 5: return c.g;
        case 6: return c.b;
        default: return luminance(c);
    }
}

vec3 gradientAt(const std::vector<GradientKnot>& ks, float t, bool cycle) {
    if (ks.empty()) return vec3(t);
    if (cycle) t -= std::floor(t);
    t = std::clamp(t, 0.0f, 1.0f);
    // Los nudos pueden venir desordenados.
    const GradientKnot* lo = nullptr;
    const GradientKnot* hi = nullptr;
    for (const GradientKnot& k : ks) {
        if (k.pos <= t && (!lo || k.pos > lo->pos)) lo = &k;
        if (k.pos >= t && (!hi || k.pos < hi->pos)) hi = &k;
    }
    if (!lo) return hi->color;
    if (!hi || hi == lo || hi->pos - lo->pos < 1e-6f) return lo->color;
    float f = (t - lo->pos) / (hi->pos - lo->pos);
    const float bias = std::clamp(lo->bias, 0.01f, 0.99f);
    if (std::fabs(bias - 0.5f) > 1e-3f) f = std::pow(f, std::log(0.5f) / std::log(bias));
    if (lo->interpolation != 3) f = f * f * (3.0f - 2.0f * f); // suave / cubica
    return glm::mix(lo->color, hi->color, f);
}

// BlendMode de C4D (1 Normal ... 20 Mascara alfa) de `top` sobre `base`.
vec3 blendMode(int mode, const vec3& base, const vec3& top) {
    auto ch = [&](auto&& f) { return vec3(f(base.r, top.r), f(base.g, top.g), f(base.b, top.b)); };
    switch (mode) {
        case 2: return base * top;
        case 3: return vec3(1.0f) - (vec3(1.0f) - base) * (vec3(1.0f) - top);
        case 4: return ch([](float b, float t) { return b < 0.5f ? 2 * b * t : 1 - 2 * (1 - b) * (1 - t); });
        case 5: return ch([](float b, float t) { return t < 0.5f ? 2 * b * t : 1 - 2 * (1 - b) * (1 - t); });
        case 6: return ch([](float b, float t) { return (1 - 2 * t) * b * b + 2 * t * b; });
        case 7: return ch([](float b, float t) { return t >= 1.0f ? 1.0f : std::min(1.0f, b / (1 - t)); });
        case 8: return ch([](float b, float t) { return t <= 0.0f ? 0.0f : std::max(0.0f, 1 - (1 - b) / t); });
        case 9: return glm::min(base, top);
        case 10: return glm::max(base, top);
        case 11: return base + top;
        case 12: return glm::max(base - top, vec3(0.0f));
        case 13: return glm::abs(base - top);
        case 14: return base + top - 2.0f * base * top;
        case 15: { vec3 b = rgbToHsl(base), t = rgbToHsl(top); return hslToRgb(vec3(t.x, b.y, b.z)); }
        case 16: { vec3 b = rgbToHsl(base), t = rgbToHsl(top); return hslToRgb(vec3(b.x, t.y, b.z)); }
        case 17: { vec3 b = rgbToHsl(base), t = rgbToHsl(top); return hslToRgb(vec3(t.x, t.y, b.z)); }
        case 18: { vec3 b = rgbToHsl(base), t = rgbToHsl(top); return hslToRgb(vec3(b.x, b.y, t.z)); }
        case 19: return base + top - vec3(1.0f); // Levr
        case 20: return base;                    // Mascara alfa: se trata aparte
        default: return top;                     // Normal
    }
}

// --- Evaluacion -----------------------------------------------------------------

struct Images {
    std::map<std::string, TextureImage> byPath; // ruta tal cual -> imagen (vacia si no carga)
    const std::string& baseDir;
    const TextureImage* get(const std::string& raw) {
        auto it = byPath.find(raw);
        if (it == byPath.end()) {
            TextureImage img;
            const std::string p = resolveTexturePath(raw, baseDir);
            if (p.empty() || !loadTextureImage(p, img)) std::fprintf(stderr, "[Iron3D] Textura no encontrada: %s\n", raw.c_str());
            it = byPath.emplace(raw, std::move(img)).first;
        }
        return it->second.width > 0 ? &it->second : nullptr;
    }
};

vec4 sampleBilinear(const TextureImage& img, float u, float v) {
    const float x = u * img.width - 0.5f, y = v * img.height - 0.5f;
    const int x0 = (int)std::floor(x), y0 = (int)std::floor(y);
    const float fx = x - x0, fy = y - y0;
    auto px = [&](int xi, int yi) {
        xi %= img.width;
        yi %= img.height;
        if (xi < 0) xi += img.width;
        if (yi < 0) yi += img.height;
        const uint8_t* p = &img.rgba[((size_t)yi * img.width + xi) * 4];
        return vec4(p[0], p[1], p[2], p[3]) / 255.0f;
    };
    return glm::mix(glm::mix(px(x0, y0), px(x0 + 1, y0), fx), glm::mix(px(x0, y0 + 1), px(x0 + 1, y0 + 1), fx), fy);
}

float gradientParam(const ShaderNode& n, float u, float v) {
    float cu = u - 0.5f, cv = v - 0.5f;
    if (std::fabs(n.angle) > 1e-5f) { // giro alrededor del centro
        const float c = std::cos(n.angle), s = std::sin(n.angle);
        const float ru = c * cu - s * cv, rv = s * cu + c * cv;
        cu = ru;
        cv = rv;
    }
    switch (n.gradientType) {
        case 2001: return cv + 0.5f;                                                 // 2D-V
        case 2002: return 0.5f * (cu + cv) + 0.5f;                                   // 2D-Diagonal
        case 2003: return std::atan2(cv, cu) / 6.2831853f + 0.5f;                    // 2D-Radial
        case 2004: return std::min(1.0f, 2.0f * std::sqrt(cu * cu + cv * cv));       // 2D-Circular
        case 2005: return std::min(1.0f, 2.0f * std::max(std::fabs(cu), std::fabs(cv))); // 2D-Caja
        default: return cu + 0.5f;                                                   // 2D-U y los 3D (aprox.)
    }
}

vec4 evalNode(const ShaderNode& n, float u, float v, Images& imgs, int depth = 0) {
    if (depth > 16) return vec4(0.0f, 0.0f, 0.0f, 1.0f);
    switch (n.kind) {
        case ShaderKind::Bitmap: {
            const TextureImage* img = imgs.get(n.path);
            return img ? sampleBilinear(*img, u, v) : vec4(0.0f, 0.0f, 0.0f, 1.0f);
        }
        case ShaderKind::Color: return vec4(n.color * n.brightness, 1.0f);
        case ShaderKind::Filter: {
            vec4 c = n.children.empty() ? vec4(0.0f, 0.0f, 0.0f, 1.0f) : evalNode(n.children[0], u, v, imgs, depth + 1);
            vec3 rgb = vec3(c);
            if (std::fabs(n.hue) > 1e-4f || std::fabs(n.saturation) > 1e-4f || std::fabs(n.lightness) > 1e-4f || n.colorize)
                rgb = adjustHsl(rgb, n.hue, n.saturation, n.lightness, n.colorize);
            rgb = brightContrastGamma(rgb, n.fBrightness, n.contrast, n.gamma);
            if (n.clipping && n.highClip - n.lowClip > 1e-4f) rgb = (rgb - vec3(n.lowClip)) / (n.highClip - n.lowClip);
            return vec4(glm::clamp(rgb, 0.0f, 1.0f), c.a);
        }
        case ShaderKind::Colorizer: {
            vec4 c = n.children.empty() ? vec4(0.0f, 0.0f, 0.0f, 1.0f) : evalNode(n.children[0], u, v, imgs, depth + 1);
            const int which = std::clamp(n.input - 2001, 0, 6);
            return vec4(gradientAt(n.gradient, channelInput(vec3(c), which), n.cycle), c.a);
        }
        case ShaderKind::Gradient: return vec4(gradientAt(n.gradient, gradientParam(n, u, v), n.cycle), 1.0f);
        case ShaderKind::Layer: {
            vec3 acc(0.0f);
            float alpha = 1.0f;
            // La lista de C4D va de arriba (indice 0) a abajo: se compone desde la ultima.
            for (auto it = n.layers.rbegin(); it != n.layers.rend(); ++it) {
                const ShaderLayer& l = *it;
                if (!l.active) continue;
                switch (l.type) {
                    case 2: { // shader
                        if (l.child < 0 || l.child >= (int)n.children.size()) break;
                        vec4 t = evalNode(n.children[(size_t)l.child], u, v, imgs, depth + 1);
                        vec3 top = vec3(t);
                        if (l.flags & 1) top = vec3(1.0f) - top;
                        if (l.mode == 20) { alpha *= luminance(top); break; }
                        float op = std::clamp(l.blend, 0.0f, 1.0f);
                        if (l.flags & 2) op *= (l.flags & 4) ? 1.0f - t.a : t.a; // Alfa / Invertir Alfa
                        acc = glm::mix(acc, blendMode(l.mode, acc, top), op);
                        break;
                    }
                    case 3: acc = brightContrastGamma(acc, l.a, l.b, l.c); break;
                    case 4: acc = adjustHsl(acc, l.a, l.b, l.c, l.colorize != 0); break;
                    case 5: { // posterizar
                        const float lv = std::max(2.0f, l.a);
                        acc = glm::floor(glm::clamp(acc, 0.0f, 1.0f) * (lv - 1.0f) + 0.5f) / (lv - 1.0f);
                        break;
                    }
                    case 6: acc = gradientAt(l.gradient, channelInput(acc, std::clamp(l.colorize, 0, 6)), l.cycle); break;
                    case 7: acc = glm::clamp(acc, vec3(l.a), vec3(l.b)); break;
                    case 8:
                        if (l.b - l.a > 1e-4f) acc = (acc - vec3(l.a)) / (l.b - l.a);
                        break;
                    default: break; // carpeta, distorsion, transformar: sin evaluar
                }
                acc = glm::clamp(acc, 0.0f, 1.0f);
            }
            return vec4(acc, alpha);
        }
        default:
            // Shader que Iron3D no evalua: si envuelve a otro (Normalizador, Fusion...),
            // se deja pasar lo de dentro; si es un procedural suelto, gris medio.
            if (!n.children.empty()) return evalNode(n.children[0], u, v, imgs, depth + 1);
            return vec4(0.5f, 0.5f, 0.5f, 1.0f);
    }
}

void gatherSize(const ShaderNode& n, Images& imgs, int& w, int& h) {
    if (n.kind == ShaderKind::Bitmap) {
        if (const TextureImage* img = imgs.get(n.path)) {
            w = std::max(w, img->width);
            h = std::max(h, img->height);
        }
    }
    for (const ShaderNode& c : n.children) gatherSize(c, imgs, w, h);
}

bool defaultMix(const ChannelSettings& c) {
    return std::fabs(c.brightness - 1.0f) < 1e-5f && c.mixMode == 0 && std::fabs(c.mixStrength - 1.0f) < 1e-5f;
}

// El arbol a evaluar (con la textura del canal como Imagen si no hay arbol).
ShaderNode treeFor(const Material& m, TexSlot s) {
    if (const ShaderNode* sh = effectiveShader(m, s)) {
        // Solo procedurales que Iron3D no evalua (Ruido...): mejor el color plano que un gris.
        if (!sh->firstBitmap() && sh->hasUnsupported()) return {};
        return *sh;
    }
    ShaderNode b;
    if (!m.texture(s).empty()) {
        b.kind = ShaderKind::Bitmap;
        b.path = m.texture(s);
    }
    return b;
}

bool needsBake(const Material& m, TexSlot s, const ShaderNode& tree) {
    if (tree.empty()) return false;
    if (!tree.isPlainBitmap()) return true;
    return colorMixChannel(s) && !defaultMix(m.channel(s));
}

} // namespace

const ShaderNode* effectiveShader(const Material& m, TexSlot s) {
    const ShaderNode& sh = m.channel(s).shader;
    if (sh.empty()) return nullptr;
    const ShaderNode* b = sh.firstBitmap();
    if (b) return b->path == m.texture(s) ? &sh : nullptr;
    // Sin imagen (Degradado, Color...): vale mientras el canal no tenga una textura puesta a mano.
    return m.texture(s).empty() ? &sh : nullptr;
}

bool channelHasTexture(const Material& m, TexSlot s) { return !treeFor(m, s).empty(); }

std::string channelTextureKey(const Material& m, TexSlot s) {
    const ShaderNode tree = treeFor(m, s);
    if (tree.empty()) return {};
    std::string key = textureCacheKey(m.texture(s), m.texBaseDir);
    if (!needsBake(m, s, tree)) return key;
    const ChannelSettings& c = m.channel(s);
    std::ostringstream o;
    o.imbue(std::locale::classic());
    o << "\n#bake " << (int)s;
    if (colorMixChannel(s))
        o << ' ' << c.color.r << ',' << c.color.g << ',' << c.color.b << ' ' << c.brightness << ' ' << c.mixMode << ' '
          << c.mixStrength;
    o << ' ' << encodeShaderTree(tree);
    return key + o.str();
}

bool loadChannelTexture(const Material& m, TexSlot s, TextureImage& out) {
    out = {};
    const ShaderNode tree = treeFor(m, s);
    if (tree.empty()) return false;
    Images imgs{{}, m.texBaseDir};
    if (!needsBake(m, s, tree)) {
        const std::string p = resolveTexturePath(tree.path, m.texBaseDir);
        return !p.empty() && loadTextureImage(p, out);
    }
    int w = 0, h = 0;
    gatherSize(tree, imgs, w, h);
    if (tree.firstBitmap() && w == 0) return false; // ninguna imagen del arbol se encuentra
    if (w == 0 || h == 0) w = h = 512;              // solo procedural (Degradado, Color...)
    const int maxSide = 4096;
    if (w > maxSide || h > maxSide) {
        const float k = (float)maxSide / (float)std::max(w, h);
        w = std::max(1, (int)(w * k));
        h = std::max(1, (int)(h * k));
    }
    const ChannelSettings& c = m.channel(s);
    const bool mix = colorMixChannel(s);
    const vec3 base = c.color * c.brightness;
    const float st = std::clamp(c.mixStrength, 0.0f, 1.0f);
    out.width = w;
    out.height = h;
    out.rgba.resize((size_t)w * h * 4);
    for (int y = 0; y < h; ++y) {
        const float v = (y + 0.5f) / h;
        for (int x = 0; x < w; ++x) {
            const float u = (x + 0.5f) / w;
            vec4 t = evalNode(tree, u, v, imgs);
            vec3 rgb = vec3(t);
            if (mix) {
                const vec3 tb = rgb * c.brightness;
                switch (c.mixMode) {
                    case 1: rgb = base + tb * st; break;
                    case 2: rgb = base - tb * st; break;
                    case 3: rgb = glm::mix(base, base * rgb, st); break;
                    default: rgb = glm::mix(base, tb, st); break;
                }
            }
            rgb = glm::clamp(rgb, 0.0f, 1.0f);
            uint8_t* p = &out.rgba[((size_t)y * w + x) * 4];
            p[0] = (uint8_t)std::lround(rgb.r * 255.0f);
            p[1] = (uint8_t)std::lround(rgb.g * 255.0f);
            p[2] = (uint8_t)std::lround(rgb.b * 255.0f);
            p[3] = (uint8_t)std::lround(std::clamp(t.a, 0.0f, 1.0f) * 255.0f);
        }
    }
    return true;
}

} // namespace iron3d
