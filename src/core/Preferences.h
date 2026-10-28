#pragma once

// Preferencias de usuario (Fase 3c): por ahora solo que backend de render usar.
// Formato de texto propio igual de simple que .iron pero plano (sin arbol):
// una linea "IRON3D_PREFS <version>" y luego "clave=valor" por linea. Se
// aplica solo al reiniciar (igual que Cinema 4D exige reiniciar al cambiar de
// API de render) -- no hay hot-swap de backend en caliente.

#include "gfx/IRenderer.h"

#include <filesystem>

namespace iron3d {

struct Preferences {
    RendererBackend renderBackend = RendererBackend::Vulkan;
};

// Si el archivo no existe o no parsea, devuelve Preferences{} (default
// Vulkan) sin tocar nada -- primer arranque o archivo corrupto, mismo
// comportamiento.
Preferences loadPreferences(const std::filesystem::path& path);
bool savePreferences(const std::filesystem::path& path, const Preferences& prefs);

} // namespace iron3d
