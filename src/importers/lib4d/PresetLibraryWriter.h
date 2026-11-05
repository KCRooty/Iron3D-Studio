#pragma once

// Escritor propio de librerias de preestablecidos de Cinema 4D (.lib4d).
//
// Mismo formato que lee PresetLibrary (deducido de archivos reales; ver
// docs/devlog/2026-11-05-escribir-lib4d.md). Escribe registros con la version
// de R19.024 y los valida el propio Cinema 4D R19 (extractor como juez).
//
// Como C4D, nunca reescribe lo que ya hay: anade bloques nuevos al final del
// archivo (antes del "dbfe" de cierre). Asi funciona igual con una libreria de
// 1 MB que con una de 900 MB.

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace iron3d {

namespace lib4d {

// Algo que anadir a una libreria. `path` es la ruta dentro de la libreria,
// con "/" ("Personajes/Freddy", "tex/piel.png"). Sin datos = carpeta vacia.
// Con datos que empiezan por "QC4D" (un .c4d) = modelo, comprimido con zlib;
// cualquier otro archivo (texturas...) se guarda tal cual.
struct NewEntry {
    std::string path;
    std::vector<uint8_t> data;
};

// CRC-32 de la url en minusculas codificada en UTF-16BE: el numero que C4D
// guarda en cada registro junto a la url (coincide en todas las librerias
// reales probadas).
int32_t urlHash(const std::string& urlUtf8);

} // namespace lib4d

// Crea una libreria vacia (solo la raiz y la cabecera) con ese titulo.
// Falla si el archivo ya existe.
bool createPresetLibrary(const std::filesystem::path& file, const std::string& title,
                         std::string* outError = nullptr);

// Anade carpetas y archivos al final de una libreria existente. Las carpetas
// intermedias que falten se crean. Falla (sin tocar el archivo) si alguna ruta
// ya existe en la libreria.
bool appendToPresetLibrary(const std::filesystem::path& file, const std::vector<lib4d::NewEntry>& entries,
                           std::string* outError = nullptr);

} // namespace iron3d
