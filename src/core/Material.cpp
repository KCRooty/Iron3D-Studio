#include "Material.h"

#include <algorithm>
#include <cmath>
#include <filesystem>

namespace iron3d {

const Material& Material::defaultMaterial() {
    static const Material kDefault = [] {
        Material m;
        m.name = "Sin material";
        m.baseColor = {0.7f, 0.7f, 0.7f};
        m.metallic = 0.0f;
        m.roughness = 0.5f;
        return m;
    }();
    return kDefault;
}

const Material& materialOrDefault(const std::vector<Material>& materials, int index) {
    if (index < 0 || (size_t)index >= materials.size()) return Material::defaultMaterial();
    return materials[(size_t)index];
}

const char* texSlotName(TexSlot slot) {
    switch (slot) {
        case TexSlot::Color: return "Color";
        case TexSlot::Roughness: return "Rugosidad";
        case TexSlot::Normal: return "Normal";
        case TexSlot::Bump: return "Relieve";
        case TexSlot::Alpha: return "Alfa";
        case TexSlot::Emission: return "Luminancia";
        case TexSlot::Transparency: return "Transparencia";
        default: return "?";
    }
}

const char* texSlotCode(TexSlot slot) {
    switch (slot) {
        case TexSlot::Color: return "color";
        case TexSlot::Roughness: return "rough";
        case TexSlot::Normal: return "normal";
        case TexSlot::Bump: return "bump";
        case TexSlot::Alpha: return "alpha";
        case TexSlot::Emission: return "emit";
        case TexSlot::Transparency: return "transp";
        default: return "?";
    }
}

namespace {
// Valores de C4D (REFLECTION_DISTRIBUTION_* / REFLECTION_FRESNEL_*), copiados
// como numeros para que core no dependa del SDK.
constexpr int kDistLambert = 6, kDistOrenNayar = 7, kDistSpecBlinn = 0, kDistSpecPhong = 1, kDistGgx = 3;
constexpr int kFresnelNone = 0, kFresnelDielectric = 1, kFresnelConductor = 2;

glm::vec3 clamp01(const glm::vec3& v) { return glm::clamp(v, glm::vec3(0.0f), glm::vec3(1.0f)); }
} // namespace

bool pbrFromLayers(const std::vector<ReflectLayer>& layers, bool colorOn, glm::vec3& baseColor, float& metallic,
                   float& roughness) {
    for (const ReflectLayer& l : layers) {
        if (l.transparencyLayer) continue;
        if (l.distribution == kDistLambert || l.distribution == kDistOrenNayar) continue;
        // Las capas "Specular" heredadas solo dan el brillo, no un reflejo de verdad.
        const bool legacy = l.distribution == kDistSpecBlinn || l.distribution == kDistSpecPhong;
        roughness = std::clamp(legacy ? 0.5f : l.roughness, 0.0f, 1.0f);
        metallic = 0.0f;
        if (l.fresnel == kFresnelConductor) {
            metallic = 1.0f;
            baseColor = clamp01(l.color);
        } else if (l.fresnel == kFresnelNone && !legacy) {
            // Sin Fresnel el reflejo es igual en todos los angulos: se
            // comporta como un metal en la proporcion de su fuerza.
            metallic = std::clamp(l.strength, 0.0f, 1.0f);
            if (!colorOn || metallic >= 0.5f) baseColor = clamp01(l.color) * metallic;
        }
        return true;
    }
    return false;
}

ReflectLayer layerFromPbr(const Material& m) {
    const bool metal = m.metallic >= 0.999f;
    const bool partial = !metal && m.metallic > 0.001f;
    ReflectLayer l;
    l.name = "Iron3D";
    l.distribution = kDistGgx;
    l.roughness = m.roughness;
    l.strength = partial ? m.metallic : 1.0f;
    l.color = metal || partial ? m.baseColor : glm::vec3(1.0f);
    l.fresnel = metal ? kFresnelConductor : (partial ? kFresnelNone : kFresnelDielectric);
    l.roughnessTex = m.texture(TexSlot::Roughness);
    return l;
}

glm::vec3 colorChannelFromPbr(const Material& m) { return m.metallic >= 0.999f ? glm::vec3(0.0f) : m.baseColor; }

bool layersMatchPbr(const Material& m) {
    if (m.layers.empty()) return false;
    glm::vec3 c = m.baseColor;
    float metal = 0.0f, rough = 0.5f;
    if (!pbrFromLayers(m.layers, true, c, metal, rough)) {
        metal = 0.0f;
        rough = 0.5f;
    }
    // Textura de rugosidad: la de la primera capa que refleja.
    std::string roughTex;
    for (const ReflectLayer& l : m.layers)
        if (!l.transparencyLayer && l.distribution != kDistLambert && l.distribution != kDistOrenNayar) {
            roughTex = l.roughnessTex;
            break;
        }
    auto near = [](float a, float b) { return std::fabs(a - b) <= 1e-6f; };
    // El color solo cuenta en metales (en dielectricos sale del canal Color).
    const bool colorOk = m.metallic < 0.5f || glm::all(glm::lessThanEqual(glm::abs(c - m.baseColor), glm::vec3(1e-6f)));
    return near(metal, m.metallic) && near(rough, m.roughness) && colorOk && roughTex == m.texture(TexSlot::Roughness);
}

std::string resolveTexturePath(const std::string& path, const std::string& baseDir) {
    if (path.empty()) return {};
    namespace fs = std::filesystem;
    auto u8 = [](const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())); };
    auto str = [](const fs::path& p) {
        const std::u8string u = p.u8string();
        return std::string(u.begin(), u.end());
    };
    std::error_code ec;
    std::string norm = path;
    std::replace(norm.begin(), norm.end(), '\\', '/');
    const fs::path p = u8(norm);
    if (p.is_absolute() && fs::is_regular_file(p, ec)) return str(p);
    if (baseDir.empty()) return {};
    const fs::path base = u8(baseDir);
    const fs::path leaf = p.filename();
    const fs::path candidates[] = {base / p, base / "tex" / p, base / "tex" / leaf, base / leaf, base.parent_path() / "tex" / leaf};
    for (const fs::path& c : candidates)
        if (!c.empty() && fs::is_regular_file(c, ec)) return str(c.lexically_normal());
    return {};
}

} // namespace iron3d
