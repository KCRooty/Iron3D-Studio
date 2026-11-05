// Iconos de los menus principales y de los gestores.

#include "IconPen.h"

namespace iron3d::icons::detail {

namespace {

// Flecha de cursor blanca (abajo a la derecha de las herramientas de seleccion).
void cursorArrow(const Pen& P, float x, float y, float k = 0.42f) {
    P.line(x + 0.16f * k, y + 0.50f * k, x + 0.36f * k, y + 0.95f * k, P.c(30, 30, 30), 3.2f);
    P.line(x + 0.16f * k, y + 0.50f * k, x + 0.36f * k, y + 0.95f * k, P.c(245, 245, 245), 1.8f);
    const float xy[] = {x, y, x, y + 0.80f * k, x + 0.58f * k, y + 0.56f * k};
    P.polyFill(xy, 3, P.c(245, 245, 245));
    P.poly(xy, 3, P.c(30, 30, 30), 0.9f, true);
}

// Rectangulo discontinuo.
void dashedRect(const Pen& P, float x0, float y0, float x1, float y1, ImU32 col, float th = 1.2f, float dash = 0.08f) {
    for (float x = x0; x < x1; x += dash * 2) {
        const float xe = std::min(x + dash, x1);
        P.line(x, y0, xe, y0, col, th);
        P.line(x, y1, xe, y1, col, th);
    }
    for (float y = y0; y < y1; y += dash * 2) {
        const float ye = std::min(y + dash, y1);
        P.line(x0, y, x0, ye, col, th);
        P.line(x1, y, x1, ye, col, th);
    }
}

// Hoja de papel con esquina doblada.
void page(const Pen& P, float x0, float y0, float x1, float y1) {
    const float f = (x1 - x0) * 0.32f;
    const ImU32 paper = P.c(235, 235, 235), edge = P.c(120, 120, 120);
    P.fill(x0, y0, x1 - f, y1, paper);
    P.fill(x1 - f, y0 + f, x1, y1, paper);
    P.tri(x1 - f, y0, x1 - f, y0 + f, x1, y0 + f, P.c(190, 190, 190));
    const float xy[] = {x0, y0, x1 - f, y0, x1, y0 + f, x1, y1, x0, y1};
    P.poly(xy, 5, edge, 0.9f, true);
}

void folder(const Pen& P, float x0, float y0, float x1, float y1) {
    const float h = y1 - y0;
    P.fill(x0, y0, x0 + (x1 - x0) * 0.45f, y0 + h * 0.25f, P.c(200, 150, 40), 0.03f);
    P.fill(x0, y0 + h * 0.15f, x1, y1, P.c(240, 190, 60), 0.03f);
    P.fill(x0, y0 + h * 0.35f, x1, y1, P.c(252, 210, 90), 0.03f);
    P.rect(x0, y0 + h * 0.15f, x1, y1, P.c(150, 105, 20), 0.8f);
}

void floppy(const Pen& P, float x0, float y0, float x1, float y1) {
    const float w = x1 - x0, h = y1 - y0;
    P.fill(x0, y0, x1, y1, P.c(70, 110, 200), 0.04f);
    P.fill(x0 + w * 0.22f, y0, x1 - w * 0.22f, y0 + h * 0.34f, P.c(210, 210, 215));
    P.fill(x1 - w * 0.42f, y0 + h * 0.05f, x1 - w * 0.30f, y0 + h * 0.28f, P.c(70, 110, 200));
    P.fill(x0 + w * 0.15f, y0 + h * 0.52f, x1 - w * 0.15f, y1 - h * 0.06f, P.c(240, 240, 240));
    P.rect(x0, y0, x1, y1, P.c(30, 50, 110), 0.8f);
}

// Lapiz de (x0,y0) (goma) a (x1,y1) (punta).
void pencil(const Pen& P, float x0, float y0, float x1, float y1) {
    const float dx = x1 - x0, dy = y1 - y0;
    const float nx = -dy * 0.18f, ny = dx * 0.18f;
    const float bx = x1 - dx * 0.2f, by = y1 - dy * 0.2f;
    P.quad(x0 + nx, y0 + ny, x0 - nx, y0 - ny, bx - nx, by - ny, bx + nx, by + ny, P.c(240, 180, 50));
    P.tri(bx - nx, by - ny, bx + nx, by + ny, x1, y1, P.c(60, 60, 60));
}

void greenPlus(const Pen& P, float cx, float cy, float r) {
    P.disc(cx, cy, r, P.c(40, 40, 40));
    P.line(cx - r * 0.6f, cy, cx + r * 0.6f, cy, P.c(110, 220, 90), 2.2f);
    P.line(cx, cy - r * 0.6f, cx, cy + r * 0.6f, P.c(110, 220, 90), 2.2f);
}

// Marco de ventanita de gestor (barra de titulo gris + area interior).
void windowFrame(const Pen& P) {
    P.fill(0.08f, 0.12f, 0.92f, 0.88f, P.c(48, 48, 52), 0.03f);
    P.fill(0.08f, 0.12f, 0.92f, 0.26f, P.c(130, 130, 136), 0.03f);
    P.rect(0.08f, 0.12f, 0.92f, 0.88f, P.c(180, 180, 185), 0.9f);
}

void ball(const Pen& P, float cx, float cy, float r, float red, float g, float b) {
    materialBall(P.dl, P.p(cx, cy), r * P.s, ImVec4(red, g, b, P.alpha), 0.6f, 40.0f);
}

} // namespace

bool drawMenuIcon(const Pen& P, Icon icon) {
    const Ink k(P);
    switch (icon) {
        // --- Seleccion -------------------------------------------------------------
        case Icon::MnuRectSelect:
            P.rect(0.10f, 0.10f, 0.66f, 0.66f, k.orange, 2.0f);
            cursorArrow(P, 0.56f, 0.52f);
            return true;
        case Icon::MnuLassoSelect: {
            float xy[40];
            for (int i = 0; i < 20; ++i) {
                const float a = (float)i * 2.0f * kPi / 20.0f;
                const float r = 0.27f + 0.06f * std::sin(a * 3.0f);
                xy[2 * i] = 0.38f + std::cos(a) * r * 1.1f;
                xy[2 * i + 1] = 0.36f + std::sin(a) * r * 0.85f;
            }
            P.poly(xy, 20, k.orange, 2.0f, true);
            P.line(0.30f, 0.60f, 0.24f, 0.86f, k.orange, 1.6f);
            cursorArrow(P, 0.56f, 0.52f);
            return true;
        }
        case Icon::MnuPolySelect: {
            const float xy[] = {0.12f, 0.20f, 0.44f, 0.08f, 0.70f, 0.30f, 0.52f, 0.64f, 0.18f, 0.58f};
            P.poly(xy, 5, k.orange, 2.0f, true);
            for (int i = 0; i < 5; ++i) P.disc(xy[2 * i], xy[2 * i + 1], 0.045f, k.orange);
            cursorArrow(P, 0.56f, 0.52f);
            return true;
        }

        // --- Archivo ---------------------------------------------------------------
        case Icon::MnuNew:
            page(P, 0.18f, 0.08f, 0.76f, 0.90f);
            greenPlus(P, 0.72f, 0.74f, 0.17f);
            return true;
        case Icon::MnuOpen:
            folder(P, 0.08f, 0.18f, 0.92f, 0.84f);
            P.quad(0.20f, 0.84f, 0.32f, 0.48f, 0.98f, 0.48f, 0.86f, 0.84f, P.c(255, 222, 120));
            return true;
        case Icon::MnuSave:
            floppy(P, 0.12f, 0.12f, 0.88f, 0.88f);
            return true;
        case Icon::MnuSaveAs:
            floppy(P, 0.08f, 0.08f, 0.76f, 0.76f);
            pencil(P, 0.94f, 0.48f, 0.56f, 0.92f);
            return true;
        case Icon::MnuExport:
            page(P, 0.10f, 0.08f, 0.62f, 0.90f);
            P.line(0.42f, 0.50f, 0.84f, 0.50f, k.green, 2.4f);
            P.head(0.94f, 0.50f, 1, 0, 0.18f, k.green);
            return true;
        case Icon::MnuExit:
            P.fill(0.14f, 0.10f, 0.56f, 0.90f, P.c(140, 90, 50));
            P.rect(0.14f, 0.10f, 0.56f, 0.90f, P.c(80, 50, 25), 1.0f);
            P.disc(0.48f, 0.52f, 0.04f, k.yellow);
            P.line(0.52f, 0.50f, 0.82f, 0.50f, k.red, 2.4f);
            P.head(0.94f, 0.50f, 1, 0, 0.18f, k.red);
            return true;

        // --- Edicion ---------------------------------------------------------------
        case Icon::MnuCut:
            P.line(0.30f, 0.10f, 0.62f, 0.62f, k.gray, 1.8f);
            P.line(0.70f, 0.10f, 0.38f, 0.62f, k.gray, 1.8f);
            P.circle(0.30f, 0.76f, 0.13f, k.red, 2.0f);
            P.circle(0.70f, 0.76f, 0.13f, k.red, 2.0f);
            return true;
        case Icon::MnuCopy:
            page(P, 0.10f, 0.08f, 0.58f, 0.66f);
            page(P, 0.40f, 0.34f, 0.88f, 0.92f);
            return true;
        case Icon::MnuPaste:
            P.fill(0.14f, 0.16f, 0.78f, 0.90f, P.c(170, 120, 70), 0.04f);
            P.fill(0.34f, 0.08f, 0.58f, 0.22f, P.c(190, 190, 190), 0.03f);
            page(P, 0.40f, 0.36f, 0.90f, 0.92f);
            return true;
        case Icon::MnuDelete:
            P.line(0.20f, 0.20f, 0.80f, 0.80f, k.red, 3.2f);
            P.line(0.80f, 0.20f, 0.20f, 0.80f, k.red, 3.2f);
            return true;
        case Icon::MnuSelectAll:
            P.fill(0.16f, 0.16f, 0.84f, 0.84f, P.c(232, 150, 50, 110));
            dashedRect(P, 0.16f, 0.16f, 0.84f, 0.84f, k.orange, 1.4f);
            return true;
        case Icon::MnuDeselect:
            dashedRect(P, 0.16f, 0.16f, 0.84f, 0.84f, k.gray, 1.4f);
            return true;
        case Icon::MnuProject:
            page(P, 0.08f, 0.08f, 0.66f, 0.90f);
            gear(P, 0.66f, 0.66f, 0.26f, P.c(215, 215, 215));
            return true;
        case Icon::MnuPrefs:
            gear(P, 0.36f, 0.36f, 0.28f, k.gray);
            for (int i = 0; i < 3; ++i) {
                const float y = 0.62f + (float)i * 0.13f;
                const float sx = 0.58f + (float)i * 0.11f;
                P.line(0.52f, y, 0.94f, y, P.c(150, 150, 150), 1.2f);
                P.fill(sx, y - 0.05f, sx + 0.08f, y + 0.05f, k.orange);
            }
            return true;

        // --- Render ----------------------------------------------------------------
        case Icon::MnuRenderRegion:
            clapper(P);
            P.fill(0.52f, 0.52f, 0.94f, 0.94f, P.c(40, 40, 40, 200));
            dashedRect(P, 0.54f, 0.54f, 0.92f, 0.92f, k.orange, 1.4f, 0.07f);
            return true;
        case Icon::MnuPictureViewer:
            clapper(P);
            P.fill(0.52f, 0.54f, 0.94f, 0.94f, P.c(235, 90, 40), 0.06f);
            P.rect(0.52f, 0.54f, 0.94f, 0.94f, P.c(255, 200, 170), 1.0f);
            P.tri(0.56f, 0.90f, 0.68f, 0.70f, 0.80f, 0.90f, P.c(255, 230, 210));
            return true;
        case Icon::MnuRenderQueue:
            clapper(P);
            P.fill(0.50f, 0.50f, 0.96f, 0.96f, P.c(40, 40, 44));
            for (int i = 0; i < 3; ++i) {
                const float y = 0.57f + (float)i * 0.13f;
                P.fill(0.56f, y, 0.90f, y + 0.07f, i == 0 ? k.orange : k.gray);
            }
            return true;
        case Icon::MnuInteractiveRegion:
            clapper(P);
            P.fill(0.34f, 0.56f, 0.98f, 0.86f, P.c(30, 30, 30));
            P.text(0.66f, 0.71f, "IRR", P.c(250, 250, 250), 0.34f);
            return true;
        case Icon::MnuMakePreview:
            P.fill(0.08f, 0.16f, 0.92f, 0.78f, P.c(225, 60, 50), 0.05f);
            P.fill(0.16f, 0.24f, 0.84f, 0.70f, P.c(245, 245, 245));
            P.tri(0.40f, 0.32f, 0.66f, 0.47f, 0.40f, 0.62f, P.c(225, 60, 50));
            P.fill(0.40f, 0.80f, 0.60f, 0.90f, P.c(150, 150, 150));
            return true;

        // --- Materiales ------------------------------------------------------------
        case Icon::MnuNewMaterial:
            ball(P, 0.44f, 0.46f, 0.36f, 0.75f, 0.75f, 0.75f);
            greenPlus(P, 0.76f, 0.76f, 0.17f);
            return true;
        case Icon::MnuNewPbrMaterial:
            ball(P, 0.44f, 0.46f, 0.36f, 0.90f, 0.55f, 0.20f);
            P.text(0.44f, 0.48f, "PBR", P.c(255, 255, 255), 0.30f);
            greenPlus(P, 0.76f, 0.76f, 0.17f);
            return true;
        case Icon::MnuLoadMaterial:
            ball(P, 0.40f, 0.38f, 0.30f, 0.30f, 0.55f, 0.88f);
            folder(P, 0.44f, 0.54f, 0.94f, 0.92f);
            return true;
        case Icon::MnuMatEditor:
            ball(P, 0.42f, 0.44f, 0.34f, 0.80f, 0.35f, 0.35f);
            pencil(P, 0.94f, 0.52f, 0.56f, 0.94f);
            return true;

        // --- Gestores --------------------------------------------------------------
        case Icon::MnuObjManager:
            windowFrame(P);
            for (int i = 0; i < 3; ++i) {
                const float y = 0.38f + (float)i * 0.16f;
                isoCube(P, 0.22f, y, 0.06f, k.blueTop, k.blueL, k.blueR, k.blueEdge);
                P.line(0.34f, y, 0.82f - (float)i * 0.12f, y, i == 0 ? P.c(255, 170, 24) : k.gray, 1.4f);
            }
            return true;
        case Icon::MnuMatManager:
            windowFrame(P);
            ball(P, 0.24f, 0.58f, 0.12f, 0.80f, 0.30f, 0.30f);
            ball(P, 0.50f, 0.58f, 0.12f, 0.30f, 0.70f, 0.35f);
            ball(P, 0.76f, 0.58f, 0.12f, 0.30f, 0.50f, 0.90f);
            return true;
        case Icon::MnuTimeline:
            windowFrame(P);
            P.line(0.14f, 0.62f, 0.86f, 0.62f, k.gray, 1.0f);
            for (int i = 0; i < 9; ++i) {
                const float x = 0.16f + (float)i * 0.085f;
                P.line(x, 0.62f, x, (i % 2) ? 0.56f : 0.50f, k.gray, 0.9f);
            }
            P.line(0.46f, 0.32f, 0.46f, 0.84f, P.c(86, 170, 96), 2.0f);
            P.fill(0.40f, 0.32f, 0.52f, 0.40f, P.c(86, 170, 96));
            return true;
        case Icon::MnuAttributes:
            windowFrame(P);
            for (int i = 0; i < 3; ++i) {
                const float y = 0.38f + (float)i * 0.16f;
                P.circle(0.18f, y, 0.035f, k.gray, 0.9f);
                P.line(0.26f, y, 0.46f, y, k.gray, 1.2f);
                P.fill(0.52f, y - 0.05f, 0.84f, y + 0.05f, P.c(30, 30, 30));
            }
            return true;
        case Icon::MnuCoords:
            windowFrame(P);
            P.text(0.20f, 0.38f, "X", k.red, 0.24f);
            P.text(0.20f, 0.56f, "Y", k.green, 0.24f);
            P.text(0.20f, 0.74f, "Z", k.blue, 0.24f);
            for (int i = 0; i < 3; ++i) {
                const float y = 0.33f + (float)i * 0.18f;
                P.fill(0.32f, y, 0.84f, y + 0.10f, P.c(30, 30, 30));
            }
            return true;
        case Icon::MnuLayers: {
            const ImU32 cols[] = {P.c(225, 80, 70), P.c(90, 190, 90), P.c(80, 140, 230)};
            for (int i = 0; i < 3; ++i) {
                const float y = 0.22f + (float)i * 0.22f;
                P.quad(0.50f, y - 0.10f, 0.90f, y, 0.50f, y + 0.10f, 0.10f, y, cols[i]);
                P.quadOutline(0.50f, y - 0.10f, 0.90f, y, 0.50f, y + 0.10f, 0.10f, y, P.c(30, 30, 30), 0.8f);
            }
            return true;
        }
        case Icon::MnuContentBrowser:
            for (int y = 0; y < 2; ++y)
                for (int x = 0; x < 2; ++x) {
                    const float fx = 0.08f + (float)x * 0.44f, fy = 0.10f + (float)y * 0.44f;
                    folder(P, fx, fy, fx + 0.38f, fy + 0.36f);
                }
            return true;
        case Icon::MnuHelp:
            P.disc(0.5f, 0.5f, 0.40f, P.c(70, 120, 210));
            P.circle(0.5f, 0.5f, 0.40f, P.c(170, 200, 250), 1.0f);
            P.text(0.5f, 0.52f, "?", P.c(255, 255, 255), 0.62f);
            return true;
        case Icon::MnuAbout:
            P.disc(0.5f, 0.5f, 0.40f, P.c(90, 90, 96));
            P.circle(0.5f, 0.5f, 0.40f, k.gray, 1.0f);
            P.text(0.5f, 0.52f, "i", P.c(255, 255, 255), 0.62f);
            return true;
        case Icon::MnuLoop:
            P.arc(0.5f, 0.5f, 0.32f, -0.4f * kPi, 1.3f * kPi, k.gray, 2.2f);
            P.head(0.62f, 0.20f, 1.0f, 0.15f, 0.2f, k.gray);
            return true;

        // --- Navegador de Contenido ---
        case Icon::CbFolder:
            folder(P, 0.08f, 0.16f, 0.92f, 0.84f);
            return true;
        case Icon::CbLibrary: // libro azul con banda dorada (una .lib4d)
            P.fill(0.20f, 0.10f, 0.80f, 0.90f, P.c(60, 100, 180), 0.04f);
            P.fill(0.20f, 0.10f, 0.30f, 0.90f, P.c(35, 60, 120));
            P.fill(0.38f, 0.24f, 0.72f, 0.40f, P.c(230, 190, 80));
            P.rect(0.20f, 0.10f, 0.80f, 0.90f, P.c(20, 35, 80), 0.8f);
            return true;
        case Icon::CbCatalog: // fichas apiladas
            P.fill(0.26f, 0.12f, 0.88f, 0.70f, P.c(150, 150, 150));
            P.fill(0.18f, 0.22f, 0.80f, 0.80f, P.c(190, 190, 190));
            P.fill(0.10f, 0.32f, 0.72f, 0.90f, P.c(235, 235, 235));
            P.rect(0.10f, 0.32f, 0.72f, 0.90f, P.c(100, 100, 100), 0.8f);
            P.line(0.18f, 0.48f, 0.62f, 0.48f, P.c(120, 120, 120), 1.0f);
            P.line(0.18f, 0.62f, 0.62f, 0.62f, P.c(120, 120, 120), 1.0f);
            return true;
        case Icon::CbRecent: // reloj
            P.disc(0.5f, 0.5f, 0.40f, P.c(235, 235, 235));
            P.circle(0.5f, 0.5f, 0.40f, P.c(90, 90, 90), 1.2f);
            P.line(0.5f, 0.5f, 0.5f, 0.24f, P.c(40, 40, 40), 1.6f);
            P.line(0.5f, 0.5f, 0.68f, 0.60f, P.c(40, 40, 40), 1.6f);
            return true;
        case Icon::CbFavorites: { // estrella
            float xy[20];
            for (int i = 0; i < 10; ++i) {
                const float a = -kPi / 2 + (float)i * kPi / 5, r = (i % 2) ? 0.18f : 0.42f;
                xy[i * 2] = 0.5f + std::cos(a) * r;
                xy[i * 2 + 1] = 0.54f + std::sin(a) * r;
            }
            P.polyFill(xy, 10, P.c(250, 200, 60));
            P.poly(xy, 10, P.c(160, 110, 20), 0.8f, true);
            return true;
        }
        case Icon::CbComputer: // monitor
            P.fill(0.10f, 0.14f, 0.90f, 0.68f, P.c(70, 70, 76), 0.03f);
            P.fill(0.16f, 0.20f, 0.84f, 0.62f, P.c(90, 150, 220));
            P.fill(0.42f, 0.68f, 0.58f, 0.80f, P.c(110, 110, 110));
            P.fill(0.28f, 0.80f, 0.72f, 0.88f, P.c(140, 140, 140), 0.02f);
            return true;
        case Icon::CbPresets: // carpeta con libro: las librerias de preestablecidos
            folder(P, 0.04f, 0.18f, 0.80f, 0.80f);
            P.fill(0.50f, 0.36f, 0.94f, 0.94f, P.c(60, 100, 180), 0.03f);
            P.fill(0.50f, 0.36f, 0.58f, 0.94f, P.c(35, 60, 120));
            P.fill(0.64f, 0.48f, 0.88f, 0.58f, P.c(230, 190, 80));
            return true;
        case Icon::CbNetwork: // globo
            P.disc(0.5f, 0.5f, 0.40f, P.c(80, 140, 210));
            P.ellipse(0.5f, 0.5f, 0.16f, 0.40f, P.c(220, 235, 255), 1.0f);
            P.line(0.10f, 0.5f, 0.90f, 0.5f, P.c(220, 235, 255), 1.0f);
            P.circle(0.5f, 0.5f, 0.40f, P.c(30, 60, 110), 1.0f);
            return true;
        case Icon::CbSearchResults: // lupa sobre una hoja
            page(P, 0.08f, 0.06f, 0.66f, 0.80f);
            P.circle(0.60f, 0.56f, 0.18f, P.c(60, 60, 60), 2.0f);
            P.line(0.73f, 0.70f, 0.92f, 0.92f, P.c(60, 60, 60), 2.6f);
            return true;
        case Icon::CbModel: // documento de C4D: hoja con un cubo
            page(P, 0.12f, 0.06f, 0.88f, 0.94f);
            P.quad(0.30f, 0.46f, 0.50f, 0.36f, 0.70f, 0.46f, 0.50f, 0.56f, P.c(150, 190, 240));
            P.quad(0.30f, 0.46f, 0.50f, 0.56f, 0.50f, 0.80f, 0.30f, 0.70f, P.c(70, 120, 200));
            P.quad(0.50f, 0.56f, 0.70f, 0.46f, 0.70f, 0.70f, 0.50f, 0.80f, P.c(100, 150, 220));
            return true;
        case Icon::CbFile:
            page(P, 0.16f, 0.06f, 0.84f, 0.94f);
            for (int i = 0; i < 3; ++i)
                P.line(0.28f, 0.44f + (float)i * 0.14f, 0.72f, 0.44f + (float)i * 0.14f, P.c(150, 150, 150), 1.0f);
            return true;
        default:
            return false;
    }
}

} // namespace iron3d::icons::detail
