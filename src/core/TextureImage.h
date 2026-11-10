#pragma once

// Carga de imagenes de textura para el visor (2026-11-10): RGBA8 tal cual
// esta en el archivo (sin pasar a lineal; cada backend decide como
// interpretarla). Formatos: PNG, JPEG, BMP, TGA y PSD (stb_image, ver
// importers/lib4d/StbImpl.cpp). EXR todavia no.

#include <cstdint>
#include <string>
#include <vector>

namespace iron3d {

struct TextureImage {
    int width = 0, height = 0;
    std::vector<uint8_t> rgba; // width * height * 4, fila de arriba primero (v = 0)
};

// `path` en UTF-8 (ya resuelta, ver resolveTexturePath). false si no existe
// o no se puede decodificar.
bool loadTextureImage(const std::string& path, TextureImage& out);

// Clave de cache de una textura de material: la ruta tal cual + la carpeta
// base. Asi el visor no repite resolveTexturePath (que mira el disco) en cada
// frame por cada malla.
std::string textureCacheKey(const std::string& rawPath, const std::string& baseDir);

} // namespace iron3d
