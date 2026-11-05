#pragma once

// Lector propio de librerias de preestablecidos de Cinema 4D (.lib4d).
//
// Formato deducido observando archivos .lib4d reales (entrada/salida, sin
// codigo ni binarios de MAXON; ver docs/devlog/2026-11-05-navegador-lib4d.md):
//
//   cabecera   "dbfh" + 12 bytes
//   bloques    etiqueta(4) + id u32 BE + tamano u64 BE + cuerpo
//     difm  cabecera de la base: url "preset://<lib>.lib4d" y titulo
//     ditm  registro de un elemento: url "preset://<lib>.lib4d/<ruta>",
//           nombre visible y tipo (1018107 = documento/modelo)
//     bina  datos del elemento: tal cual (p.ej. un PNG) o u32 BE con el
//           tamano descomprimido + flujo zlib (un .c4d completo en los modelos)
//     imag  miniatura JPEG
//     ivld  registro anulado (se ignora)
//     dbfe  fin
//   Se reescriben bloques al editar la libreria: manda el ULTIMO de cada id.
//
// Leer funciona en cualquier plataforma; importar un modelo necesita el
// importador de .c4d (Cineware, solo Windows).

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace iron3d {

enum class PresetKind { Folder, Model, File };

struct PresetItem {
    uint32_t id = 0;
    std::string path;   // ruta dentro de la libreria ("tex/bump map.png"), UTF-8
    std::string parent; // carpeta que lo contiene ("" = raiz)
    std::string name;   // nombre visible
    PresetKind kind = PresetKind::Folder;
    uint32_t typeId = 0;
    // Datos (bina) y miniatura (imag): posicion del cuerpo en el archivo.
    uint64_t dataOffset = 0, dataSize = 0;
    bool compressed = false;
    uint32_t rawSize = 0; // tamano descomprimido si `compressed`
    uint64_t thumbOffset = 0, thumbSize = 0;
    bool hasThumb() const { return thumbSize > 0; }
};

class PresetLibrary {
public:
    // Recorre el archivo sin cargarlo entero (las librerias pesan cientos de MB).
    bool open(const std::filesystem::path& file, std::string* outError = nullptr);

    const std::filesystem::path& file() const { return file_; }
    const std::string& title() const { return title_; }
    const std::vector<PresetItem>& items() const { return items_; }
    // Indices de `items()` dentro de `folder` ("" = raiz): carpetas primero,
    // despues por nombre.
    std::vector<int> children(const std::string& folder) const;
    int modelCount() const;

    // Datos del elemento (descomprimidos si hace falta).
    bool readData(const PresetItem& item, std::vector<uint8_t>& out, std::string* outError = nullptr) const;
    // Miniatura decodificada a RGBA8.
    bool readThumbnail(const PresetItem& item, std::vector<uint8_t>& rgba, int& width, int& height) const;

private:
    std::filesystem::path file_;
    std::string title_;
    std::vector<PresetItem> items_;
};

// Tipo del registro de los documentos de C4D (modelos/escenas).
constexpr uint32_t kLib4dDocumentType = 1018107;

bool isLib4dPath(const std::filesystem::path& path);

// Piezas sueltas del formato (las usan las pruebas).
namespace lib4d {
// Cadenas UTF-16BE del registro, en orden, ya en UTF-8; `ends` recibe donde
// acaba cada una.
std::vector<std::string> recordStrings(const std::vector<uint8_t>& body, std::vector<size_t>* ends = nullptr);
// Valor de la clave 1 (entero) a partir de `start`; 0 si no hay.
uint32_t recordTypeId(const std::vector<uint8_t>& body, size_t start);
} // namespace lib4d

} // namespace iron3d
