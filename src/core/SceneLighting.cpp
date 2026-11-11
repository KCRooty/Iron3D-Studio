#include "SceneLighting.h"
#include "SceneNode.h"

#include <algorithm>
#include <cmath>

namespace iron3d {

namespace {

void collect(const SceneNode& node, const glm::mat4& parentWorld, bool parentVisible, SceneLighting& out) {
    const bool vis = resolveVisibility(node.editorVis, parentVisible);
    const glm::mat4 world = parentWorld * node.localMatrix();
    if (vis && node.primitive == PrimitiveType::Light && (int)out.lights.size() < kMaxViewportLights) {
        const LightData& l = node.light;
        const glm::vec3 color = l.color * std::max(l.intensity, 0.0f);
        if (!l.noIllumination && (l.diffuse || l.specular) && color.x + color.y + color.z > 1e-5f) {
            ViewportLight v;
            v.position = glm::vec3(world[3]);
            // La luz de C4D emite por su +Z; Iron3D invierte Z al importar,
            // asi que en Iron3D emite por su -Z local.
            const glm::vec3 fwd = -glm::vec3(world[2]);
            const float len = glm::length(fwd);
            v.direction = len > 1e-8f ? fwd / len : glm::vec3(0.0f, 0.0f, -1.0f);
            v.color = color;
            v.falloff = l.falloff;
            v.radius = std::max(l.falloffRadius, 1e-4f);
            v.innerRadius = std::max(l.innerRadius, 0.0f);
            switch (l.kind) {
                case LightKind::Spot:
                case LightKind::SpotSquare: {
                    v.type = ViewportLight::Type::Spot;
                    // Angulos de C4D = apertura total del cono.
                    const float outer = glm::radians(std::clamp(l.outerAngleDeg, 0.1f, 179.9f)) * 0.5f;
                    const float inner = glm::radians(std::clamp(l.innerAngleDeg, 0.0f, l.outerAngleDeg)) * 0.5f;
                    v.cosOuter = std::cos(outer);
                    v.cosInner = std::max(std::cos(inner), v.cosOuter + 1e-4f);
                    break;
                }
                case LightKind::Infinite:
                case LightKind::Parallel:
                case LightKind::ParallelSpot:
                case LightKind::ParallelSpotSquare:
                    v.type = ViewportLight::Type::Directional;
                    v.falloff = 0; // la luz infinita no decae
                    break;
                default: // Omni, Tubo, Area y Fotometrica: puntual en el visor
                    v.type = ViewportLight::Type::Point;
                    break;
            }
            v.diffuse = l.diffuse;
            v.specular = l.specular;
            v.castsShadow = l.shadow != 0;
            if (v.castsShadow && out.shadowLight < 0) out.shadowLight = (int)out.lights.size();
            out.lights.push_back(v);
        }
    }
    for (const auto& c : node.children()) collect(*c, world, vis, out);
}

} // namespace

SceneLighting collectSceneLights(const SceneNode& root) {
    SceneLighting out;
    for (const auto& c : root.children()) collect(*c, glm::mat4(1.0f), true, out);
    return out;
}

float falloffFactor(int falloff, float d, float radius, float innerRadius) {
    radius = std::max(radius, 1e-4f);
    d = std::max(d, 1e-4f);
    switch (falloff) {
        case 5: return d <= radius ? 1.0f : 0.0f;
        case 6: return std::min(1.0f, radius / d);
        case 7: return std::min(1.0f, (radius / d) * (radius / d));
        case 8:
            if (d <= innerRadius) return 1.0f;
            return std::clamp((radius - d) / std::max(radius - innerRadius, 1e-4f), 0.0f, 1.0f);
        case 9: return radius / d;
        case 10: return (radius / d) * (radius / d);
        default: return 1.0f;
    }
}

} // namespace iron3d
