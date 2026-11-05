#pragma once

// Navegador de Contenido (como el de Cinema 4D R19): arbol a la izquierda
// (Catalogos, Elementos Recientes, Favoritos, Ordenador, Preestablecidos, Red,
// Resultado de la Busqueda) y rejilla de miniaturas a la derecha.
//
// "Preestablecidos" lista las librerias .lib4d montadas:
//  - permanentes: las que hay en <Iron3D>/library/browser/ al arrancar
//    (copiar ahi una .lib4d, o "Hacer permanente", la deja siempre a mano);
//  - temporales: las abiertas en esta sesion (Archivo > Abrir con un .lib4d,
//    o pasandola por linea de comandos). Desaparecen al cerrar Iron3D.
// Doble clic en un modelo, o arrastrarlo al visor, lo fusiona con la escena.

#include "core/Material.h"
#include "core/SceneNode.h"
#include "importers/lib4d/PresetLibrary.h"

#include <imgui.h>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>

namespace iron3d {

// Texturas de las miniaturas: main.cpp las conecta al renderer activo.
using UiTextureCreate = std::function<void*(const uint8_t* rgba, int width, int height)>;
using UiTextureDestroy = std::function<void(void*)>;
void setUiTextureFactory(UiTextureCreate create, UiTextureDestroy destroy);
// Libera todas las miniaturas (llamar antes de apagar el renderer).
void releaseContentBrowserTextures();

// Carpeta de librerias permanentes (<Iron3D>/library/browser).
std::filesystem::path permanentLibraryDir();
// Monta todas las .lib4d de permanentLibraryDir(). Llamar una vez al arrancar.
void scanPermanentLibraries();
// Monta una .lib4d para esta sesion (si ya estaba montada, la reutiliza) y
// la deja seleccionada en el navegador.
bool mountTemporaryLibrary(const std::filesystem::path& file, std::string* outError = nullptr);
// Libreria ya montada a partir de su archivo (o nullptr).
const PresetLibrary* findMountedLibrary(const std::filesystem::path& file);

// Lo que el navegador pide a quien lo dibuja (MainUi).
struct ContentBrowserCallbacks {
    // Cargar un modelo (doble clic, Intro, Edicion > Cargar, arrastrar al visor).
    std::function<void(const PresetLibrary& lib, const PresetItem& item)> load;
    // Archivo > Abrir Libreria de Preestablecidos... (dialogo del sistema).
    std::function<void()> openLibrary;
    // Archivo > Cerrar: volver al Gestor de Objetos.
    std::function<void()> close;
};

// Dibuja el navegador en el rectangulo del Gestor de Objetos.
void drawContentBrowser(ImVec2 pos, ImVec2 size, const ContentBrowserCallbacks& cb);

// Archivos soltados desde el Explorador: si el raton esta sobre el navegador
// y hay una libreria abierta, se le anaden (.c4d = modelo, el resto tal
// cual). Devuelve si los ha usado.
bool contentBrowserDropFiles(const std::vector<std::filesystem::path>& files);

// Arrastre de un modelo desde el navegador: si `payload` es uno, devuelve su
// libreria y elemento.
bool resolvePresetPayload(const ImGuiPayload* payload, const PresetLibrary*& lib, const PresetItem*& item);
constexpr const char* kPresetPayloadType = "IRON3D_PRESET";

// Apunta un modelo en "Elementos Recientes" (tras cargarlo).
void noteRecentPreset(const PresetLibrary& lib, const PresetItem& item);

// "Guardar como Preestablecido" (Gestor de Objetos): exporta `node` con sus
// hijos a .c4d en ese momento, renderiza su miniatura y abre la ventana donde
// se elige libreria, carpeta y nombre.
void beginSavePreset(const SceneNode& node, const std::vector<Material>& materials);
// Dibuja esa ventana si esta abierta (MainUi la llama cada frame).
void drawSavePresetDialog();

} // namespace iron3d
