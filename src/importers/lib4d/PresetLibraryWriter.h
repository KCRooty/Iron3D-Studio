#pragma once

// Escritor propio de librerias de preestablecidos de Cinema 4D (.lib4d).
//
// Mismo formato que lee PresetLibrary (deducido de archivos reales; ver
// docs/devlog/2026-11-05-escribir-lib4d.md). Escribe registros con la version
// de R19.024 y los valida el propio Cinema 4D R19 (extractor como juez).
//
// Como C4D, no reescribe lo que ya hay: anade bloques nuevos al final del
// archivo (antes del "dbfe" de cierre) y, para borrar, renombrar o mover,
// cambia la etiqueta del bloque viejo por "ivld" en su sitio (4 bytes). Asi
// funciona igual con una libreria de 1 MB que con una de 900 MB. Solo
// rewritePresetLibrary escribe un archivo nuevo entero.

#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace iron3d {

namespace lib4d {

// Algo que anadir a una libreria. `path` es la ruta dentro de la libreria,
// con "/" ("Personajes/Freddy", "tex/piel.png"). Sin datos = carpeta vacia.
// Con datos que empiezan por "QC4D" (un .c4d) = modelo, comprimido con zlib;
// cualquier otro archivo (texturas...) se guarda tal cual.
struct NewEntry {
    NewEntry() = default;
    NewEntry(std::string p, std::vector<uint8_t> d = {}) : path(std::move(p)), data(std::move(d)) {}

    std::string path;
    std::vector<uint8_t> data;
    // Miniatura opcional de un modelo (RGBA, opaca): se guarda en JPEG en un
    // bloque "imag", como las que renderiza C4D.
    std::vector<uint8_t> thumbRgba;
    int thumbWidth = 0, thumbHeight = 0;
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

// Borra elementos (con todo lo que haya dentro si son carpetas).
bool removeFromPresetLibrary(const std::filesystem::path& file, const std::vector<std::string>& paths,
                             std::string* outError = nullptr);

// Renombra y/o mueve un elemento: `to` es la ruta completa nueva
// ("Personajes/Freddy viejo"). Si cambia la ultima parte, cambia tambien el
// nombre visible. Las carpetas se llevan su contenido. Las carpetas de
// destino que falten se crean. Falla si `to` ya existe o esta dentro de `from`.
bool movePresetLibraryItem(const std::filesystem::path& file, const std::string& from, const std::string& to,
                           std::string* outError = nullptr);

// Escribe `dst` (nuevo, no puede existir) con lo vigente de `src`, sin los
// bloques anulados, y con la url raiz "preset://<nombre de dst>" en todos los
// registros: lo que necesita C4D para montar una libreria copiada o guardada
// con otro nombre. Con `title` no vacio cambia tambien el titulo.
bool rewritePresetLibrary(const std::filesystem::path& src, const std::filesystem::path& dst,
                          const std::string& title = {}, std::string* outError = nullptr);

} // namespace iron3d
