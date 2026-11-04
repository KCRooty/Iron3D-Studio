#include "Preferences.h"

#include <fstream>

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
    Preferences prefs; // defaults (Vulkan, preset C4D) si algo falla abajo
    std::ifstream file(path, std::ios::binary);
    if (!file) return prefs;

    std::string line;
    std::getline(file, line);
    if (line.rfind("IRON3D_PREFS", 0) != 0) return prefs; // cabecera invalida: ignora el archivo

    // Claves desconocidas o que no parsean se ignoran en silencio; un archivo
    // de antes de los presets (sin ui.* / layout.* / style.*) deja el C4D.
    while (std::getline(file, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = line.substr(0, eq);
        const std::string val = line.substr(eq + 1);
        if (key == "renderBackend") prefs.renderBackend = backendFromCode(val);
        else readPresetKey(prefs.uiPreset, key, val);
    }
    return prefs;
}

bool savePreferences(const std::filesystem::path& path, const Preferences& prefs) {
    std::ofstream file(path, std::ios::binary);
    if (!file) return false;
    file << "IRON3D_PREFS " << kFormatVersion << "\n";
    file << "renderBackend=" << backendCode(prefs.renderBackend) << "\n";
    writePresetKeys(file, prefs.uiPreset);
    return (bool)file;
}

} // namespace iron3d
