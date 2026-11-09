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
//     ivld  bloque anulado: C4D no borra nada, cambia la etiqueta del bloque
//           viejo por "ivld" en su sitio (mismo id y cuerpo) y anade el nuevo
//           al final. Asi se borra, renombra y mueve.
//     dbfe  fin
//   Los registros (difm/ditm) son contenedores serializados: ver
//   lib4d::parseRecord. Manda el ULTIMO ditm valido de cada id.
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
    uint32_t id = 0;    // id del registro (0 = carpeta que solo existe como ruta de otros)
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
    // Url raiz guardada dentro ("preset://<archivo>.lib4d"). C4D solo monta la
    // libreria si coincide con el nombre del archivo.
    const std::string& rootUrl() const { return rootUrl_; }
    bool rootUrlMatchesFile() const;
    // Elemento por ruta (sin distinguir mayusculas); -1 si no esta.
    int find(const std::string& path) const;
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
    std::string title_, rootUrl_;
    std::vector<PresetItem> items_;
};

// Tipo del registro de los documentos de C4D (modelos/escenas).
constexpr uint32_t kLib4dDocumentType = 1018107;

bool isLib4dPath(const std::filesystem::path& path);

// Piezas sueltas del formato (las usan el escritor y las pruebas).
namespace lib4d {

// Contenedor serializado de un registro ("QC4Dbrow"): cabecera de 13 bytes
// (etiqueta, version u32 BE, 0) y un contenedor. Cada valor lleva su marca:
// 0x0F entero de 32 bits, 0x11 entero de 64, 0x82 cadena (u32 bytes +
// UTF-16BE), 0x01 contenedor (u32 132, u32 1, id; elementos clave/tipo/valor;
// 0x02). Tipos: 15, 26, 130 y 132 -- los unicos que salen en 2199 registros
// de 40 librerias reales, todos leidos sin sobrar un byte.
struct BcValue {
    int32_t type = 15;               // 15 entero, 26 entero 64, 130 cadena, 132 contenedor
    int64_t i = 0;
    std::vector<uint8_t> utf16;      // cadena tal cual (sin pasar por UTF-8: ida y vuelta exacta)
    int32_t cid = -1;                // id del contenedor
    std::vector<std::pair<int32_t, BcValue>> kids;

    BcValue* find(int32_t key);
    const BcValue* find(int32_t key) const;
    std::string str() const;         // cadena en UTF-8
    void setStr(const std::string& utf8);
};
struct BcRecord {
    std::vector<uint8_t> header;     // 13 bytes
    BcValue root;
};
bool parseRecord(const std::vector<uint8_t>& body, BcRecord& out);
std::vector<uint8_t> serializeRecord(const BcRecord& rec);
std::vector<uint8_t> utf8ToUtf16be(const std::string& s);
std::string utf16beToUtf8(const uint8_t* p, size_t bytes);
// Codigos de idioma de los nombres visibles (clave 16): "es" y "us".
constexpr int32_t kLangEs = 0x65730000, kLangUs = 0x75730000;

// Cadenas UTF-16BE del registro, en orden, ya en UTF-8; `ends` recibe donde
// acaba cada una.
std::vector<std::string> recordStrings(const std::vector<uint8_t>& body, std::vector<size_t>* ends = nullptr);
// Valor de la clave 1 (entero) a partir de `start`; 0 si no hay.
uint32_t recordTypeId(const std::vector<uint8_t>& body, size_t start);
} // namespace lib4d

} // namespace iron3d
