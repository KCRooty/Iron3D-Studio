#include "Material.h"

namespace iron3d {

const Material& Material::defaultMaterial() {
    static const Material kDefault{"Sin material", {0.7f, 0.7f, 0.7f}, 0.3f, 32.0f};
    return kDefault;
}

const Material& materialOrDefault(const std::vector<Material>& materials, int index) {
    if (index < 0 || (size_t)index >= materials.size()) return Material::defaultMaterial();
    return materials[(size_t)index];
}

} // namespace iron3d
