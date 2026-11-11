#pragma once

// Texturas de los preestablecidos (2026-11-11). Un modelo de .lib4d guarda las
// rutas de sus imagenes como "preset://<libreria>.lib4d/tex/x.png" (o solo el
// nombre), y las imagenes viven dentro de esa libreria -- o de otra montada.
// Esto las busca, las saca a una carpeta propia de cada libreria (dos packs
// con un "Skin.png" cada uno ya no se pisan) y deja en el material la ruta
// del archivo extraido.

#include "core/Material.h"
#include "PresetLibrary.h"

#include <filesystem>
#include <string>
#include <vector>

namespace iron3d {

struct PresetTextureReport {
    int alreadyOnDisk = 0;   // rutas que ya resolvian sin la libreria
    int extracted = 0;       // sacadas de una .lib4d (o reutilizadas de la cache)
    std::vector<std::string> missing; // rutas que no estan en ninguna libreria
};

// `lib`: la libreria del modelo; `others`: el resto de librerias montadas
// (se miran despues). `cacheDir`: carpeta raiz de la cache de texturas.
// Toca los materiales de `firstNew` en adelante.
PresetTextureReport extractPresetTextures(const PresetLibrary& lib, const std::vector<const PresetLibrary*>& others,
                                          const std::filesystem::path& cacheDir, std::vector<Material>& materials,
                                          size_t firstNew);

} // namespace iron3d
