#pragma once

// Personalizacion de colores de la UI (pedido de Core tras ver LYNX-SHARE):
// unos pocos colores "semilla" editables desde Preferencias > Interfaz, el
// resto de la paleta (estilo ImGui + los ImU32 que usa el dibujado custom de
// MainUi.cpp) se deriva de esos pocos -- mismo truco que ThemePalette.h de
// LYNX-SHARE (ahi derivan 26 tokens de 2-3 colores). El default de UiTheme
// ES el tema C4D que ya tenia Iron3D antes de que esto existiera, asi que
// "tema por defecto" y "esencia de Cinema 4D" son la misma cosa, no una
// aproximacion.

#include <imgui.h>
#include <glm/glm.hpp>

namespace iron3d {

struct UiTheme {
    glm::vec3 panelBg = {0.235f, 0.235f, 0.235f};  // #3C3C3C -- fondo de paneles
    glm::vec3 fieldBg = {0.165f, 0.165f, 0.165f};  // fondo de campos editables
    glm::vec3 accent = {0.878f, 0.580f, 0.180f};   // #E0942E -- amber C4D
    glm::vec3 toolBlue = {0.314f, 0.565f, 0.816f}; // #5090D0 -- azul de herramientas
    glm::vec3 text = {0.847f, 0.847f, 0.847f};     // #D8D8D8
    glm::vec3 selection = {1.0f, 0.6667f, 0.0941f}; // #FFAA18 -- objeto seleccionado (nombre y contorno)
    glm::vec3 viewportBg = {0.353f, 0.353f, 0.353f}; // fondo del visor 3D
};

inline UiTheme defaultC4DTheme() { return UiTheme{}; }

// Paleta completa derivada de los 5 colores semilla de arriba: todo lo que
// applyTheme() necesita, tanto para ImGuiStyle::Colors[] como para el
// dibujado custom (ImDrawList) que antes usaba las constantes sueltas
// kAmber/kStatusBg/etc. de MainUi.cpp.
struct DerivedPalette {
    // -- estilo ImGui (antes variables locales de applyC4DTheme) --
    ImVec4 panelBg, fieldBg, border, text, textDisabled;
    ImVec4 amber, amberHover, toolBlue, selection, headerHovered;
    ImVec4 titleBg, titleBgActive, menuBarBg, scrollbarBg;
    ImVec4 scrollbarGrab, scrollbarGrabHovered, buttonCol, buttonHovered;
    ImVec4 frameBgHovered, frameBgActive, popupBg, tabDimmedSelected, textSelectedBg;

    // -- dibujado custom (antes constexpr ImU32 al principio de MainUi.cpp) --
    ImU32 amberU32, toolBlueU32, tabActiveU32, selOrangeU32, hoverRowU32;
    ImU32 textColU32, textDimU32, borderDarkU32, borderLightU32;
    ImU32 sectionBgU32, statusBgU32, rulerBgU32, playheadU32;
};

DerivedPalette derivePalette(const UiTheme& seed);

} // namespace iron3d
