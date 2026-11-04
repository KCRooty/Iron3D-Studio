#pragma once

// Presets de interfaz (Preferencias > Interfaz): colores (UiTheme), como se
// reparten los paneles (LayoutSpec), metricas de estilo y nombres de los
// paneles, imitando C4D, Blender, Maya, 3ds Max y Source Filmmaker. No cambia
// el contenido de ningun panel: solo donde va cada uno, su tamano, su color y
// como se llama. El preset "c4d" reproduce exactamente el layout y el tema de
// siempre (mismos numeros que antes eran literales en MainUi.cpp).

#include "UiTheme.h"

#include <imgui.h>

#include <filesystem>
#include <iosfwd>
#include <string>
#include <vector>

namespace iron3d {

enum class Arrangement { C4D, Blender, Maya, Max, SFM };

struct LayoutSpec {
    Arrangement arrangement = Arrangement::C4D;
    float toolbarH = 38.0f;     // barra de iconos superior
    float leftToolbarW = 38.0f; // barra de modos de la izquierda
    float tabsW = 20.0f;        // pestanas verticales del borde derecho
    float sideFrac = 0.30f, sideMin = 330.0f, sideMax = 520.0f; // columna Atributos (y Objetos si van apilados)
    float outlinerW = 230.0f;   // columna propia de Objetos (Maya / 3ds Max)
    float objFrac = 0.38f;      // Objetos sobre Atributos cuando comparten columna
    float shelfFrac = 0.17f, shelfMin = 150.0f, shelfMax = 200.0f; // fila Materiales + Coordenadas
    float timelineH = 56.0f;
    float timelineFrac = 0.0f;  // >0: alto de la Linea de Tiempo como fraccion de la ventana (SFM)
    bool verticalTabs = true;
};

struct StyleMetrics {
    float frameRounding = 2.0f, windowRounding = 0.0f, popupRounding = 2.0f, grabRounding = 2.0f;
    float tabRounding = 0.0f;
    float framePadX = 6.0f, framePadY = 3.0f;
};

struct UiPreset {
    std::string id = "c4d"; // c4d | blender | maya | max | sfm | user:<nombre>
    std::string name = "Cinema 4D";
    UiTheme theme;
    LayoutSpec layout;
    StyleMetrics style;
};

// Nombres de cada panel tal y como los llama el programa imitado (Blender en
// su traduccion al espanol; Maya, 3ds Max y SFM no tienen UI en espanol).
struct PanelNames {
    const char* objects;
    const char* attributes;
    const char* timeline;
    const char* materials;
    const char* coords;
    const char* viewport;
    const char* layoutLabel;
    const char* topTabs[4];
    int topTabCount;
    const char* sideTabs[3];
    int sideTabCount;
};

const char* arrangementName(Arrangement a);
const PanelNames& panelNames(Arrangement a);

const std::vector<UiPreset>& builtinPresets();
// Preset integrado por id, o nullptr.
const UiPreset* findBuiltinPreset(const std::string& id);

// Serializacion clave=valor (la comparten preferences.cfg y los .i3dui).
void writePresetKeys(std::ostream& out, const UiPreset& preset);
// true si `key` era de preset (theme.*, layout.*, style.*, ui.*).
bool readPresetKey(UiPreset& preset, const std::string& key, const std::string& val);

// Presets del usuario: <dir>/<nombre>.i3dui
std::vector<UiPreset> loadUserPresets(const std::filesystem::path& dir);
bool saveUserPreset(const std::filesystem::path& dir, const UiPreset& preset);
bool deleteUserPreset(const std::filesystem::path& dir, const std::string& name);

// Rectangulos de todos los paneles fijos para un tamano de ventana dado.
struct PanelRect {
    ImVec2 pos{0, 0}, size{0, 0};
    bool visible() const { return size.x > 0.0f && size.y > 0.0f; }
};
struct LayoutRects {
    PanelRect top, left, viewport, objects, attributes, objTabs, attrTabs, timeline, materials, coords, status;
};
LayoutRects computeLayout(const LayoutSpec& spec, ImVec2 origin, ImVec2 size, float menuH, float statusH);

} // namespace iron3d
