#include "Preferences.h"

#include <fstream>
#include <sstream>

namespace iron3d {

namespace {
constexpr int kFormatVersion = 1;

const char* backendCode(RendererBackend backend) {
    return backend == RendererBackend::Vulkan ? "vulkan" : "opengl";
}

RendererBackend backendFromCode(const std::string& code) {
    return code == "opengl" ? RendererBackend::OpenGL : RendererBackend::Vulkan;
}
} // namespace

Preferences loadPreferences(const std::filesystem::path& path) {
    Preferences prefs; // defaults (Vulkan) si algo falla abajo
    std::ifstream file(path, std::ios::binary);
    if (!file) return prefs;

    std::string line;
    std::getline(file, line);
    if (line.rfind("IRON3D_PREFS", 0) != 0) return prefs; // cabecera invalida: ignora el archivo

    while (std::getline(file, line)) {
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = line.substr(0, eq);
        const std::string val = line.substr(eq + 1);
        if (key == "renderBackend") prefs.renderBackend = backendFromCode(val);
    }
    return prefs;
}

bool savePreferences(const std::filesystem::path& path, const Preferences& prefs) {
    std::ofstream file(path, std::ios::binary);
    if (!file) return false;
    file << "IRON3D_PREFS " << kFormatVersion << "\n";
    file << "renderBackend=" << backendCode(prefs.renderBackend) << "\n";
    return true;
}

} // namespace iron3d
