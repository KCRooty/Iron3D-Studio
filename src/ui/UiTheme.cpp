#include "UiTheme.h"

#include <algorithm>

namespace iron3d {

namespace {

glm::vec3 clamp01(const glm::vec3& v) {
    return glm::vec3(std::clamp(v.x, 0.0f, 1.0f), std::clamp(v.y, 0.0f, 1.0f), std::clamp(v.z, 0.0f, 1.0f));
}

ImVec4 toVec4(const glm::vec3& v, float a = 1.0f) {
    const glm::vec3 c = clamp01(v);
    return ImVec4(c.x, c.y, c.z, a);
}

ImU32 toU32(const glm::vec3& v, int a = 255) {
    const glm::vec3 c = clamp01(v) * 255.0f;
    return IM_COL32((int)(c.x + 0.5f), (int)(c.y + 0.5f), (int)(c.z + 0.5f), a);
}

// Valores por defecto (los literales que ya tenia applyC4DTheme antes de
// esto) -- se usan solo para calcular los deltas de abajo, nunca en tiempo
// de ejecucion. Con la UiTheme por defecto (ver UiTheme.h), seed == estos
// valores, asi que cada derivado de mas abajo reproduce exactamente el
// color original.
constexpr glm::vec3 kDefPanelBg(0.235f, 0.235f, 0.235f);
constexpr glm::vec3 kDefAccent(0.878f, 0.580f, 0.180f);
constexpr glm::vec3 kDefToolBlue(0.314f, 0.565f, 0.816f);
constexpr glm::vec3 kDefText(0.847f, 0.847f, 0.847f);
constexpr float kDefBorder = 0.118f;

} // namespace

DerivedPalette derivePalette(const UiTheme& seed) {
    DerivedPalette p{};

    // --- familia "panel" (grises de fondo/chrome) ---------------------------
    const glm::vec3 panelBg = seed.panelBg;
    const glm::vec3 border3 = clamp01(panelBg + glm::vec3(kDefBorder - kDefPanelBg.x));

    p.panelBg = toVec4(panelBg);
    p.fieldBg = toVec4(seed.fieldBg);
    p.border = toVec4(border3);
    p.titleBg = toVec4(panelBg + glm::vec3(-0.035f));
    p.titleBgActive = toVec4(panelBg + glm::vec3(0.025f));
    p.menuBarBg = toVec4(panelBg + glm::vec3(-0.02f));
    p.scrollbarBg = toVec4(panelBg + glm::vec3(-0.035f));
    p.scrollbarGrab = toVec4(panelBg + glm::vec3(0.145f));
    p.scrollbarGrabHovered = toVec4(panelBg + glm::vec3(0.215f));
    p.buttonCol = toVec4(panelBg + glm::vec3(0.085f));
    p.buttonHovered = toVec4(panelBg + glm::vec3(0.145f));
    p.frameBgHovered = toVec4(panelBg + glm::vec3(-0.035f));
    p.frameBgActive = toVec4(panelBg + glm::vec3(-0.015f));
    p.popupBg = toVec4(panelBg + glm::vec3(0.035f), 0.98f);

    // --- texto ---------------------------------------------------------------
    p.text = toVec4(seed.text);
    p.textDisabled = toVec4(seed.text + glm::vec3(-0.345f));

    // --- acento (amber) --------------------------------------------------------
    const glm::vec3 accent = seed.accent;
    p.amber = toVec4(accent);
    p.amberHover = toVec4(accent + glm::vec3(0.052f, 0.080f, 0.100f));
    p.tabDimmedSelected = toVec4(accent + glm::vec3(-0.278f, -0.160f, -0.020f));
    p.textSelectedBg = toVec4(accent, 0.35f);

    // --- azul de herramientas y seleccion --------------------------------------
    const glm::vec3 toolBlue = seed.toolBlue;
    p.toolBlue = toVec4(toolBlue);
    const glm::vec3 selection3 = toolBlue + glm::vec3(-0.014f, -0.195f, -0.346f);
    p.selection = toVec4(selection3);
    p.headerHovered = toVec4(selection3 + glm::vec3(0.04f, 0.03f, 0.01f));

    // --- dibujado custom (ImU32), antes constantes sueltas en MainUi.cpp -------
    p.amberU32 = toU32(accent);
    p.toolBlueU32 = toU32(toolBlue, 150);
    p.tabActiveU32 = toU32(toolBlue + glm::vec3(0.2392f, 0.0902f, -0.0157f));
    p.selOrangeU32 = toU32(seed.selection);
    p.hoverRowU32 = IM_COL32(255, 255, 255, 14);      // overlay fijo, no depende del tema
    p.textColU32 = toU32(seed.text);
    p.textDimU32 = toU32(seed.text + glm::vec3(-0.345f));
    p.borderDarkU32 = toU32(border3);
    p.borderLightU32 = toU32(border3 + glm::vec3(0.195725f));
    p.sectionBgU32 = toU32(panelBg + glm::vec3(-0.0389f));
    p.statusBgU32 = toU32(panelBg + glm::vec3(-0.133f));
    p.rulerBgU32 = toU32(panelBg + glm::vec3(-0.0311f));
    p.playheadU32 = IM_COL32(86, 170, 96, 255);       // verde de timeline, color semantico fijo

    return p;
}

} // namespace iron3d
