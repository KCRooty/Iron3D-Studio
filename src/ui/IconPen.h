#pragma once

// Interno de los iconos (C4DIcons.cpp + Icons*.cpp): el "lapiz" con el que se
// dibuja cada icono en coordenadas normalizadas 0..1 del cuadrado del icono,
// mas las formas que se repiten entre familias. No incluir fuera de src/ui/Icons*.

#include "C4DIcons.h"

#include <imgui.h>

#include <algorithm>
#include <cfloat>
#include <cmath>

namespace iron3d::icons::detail {

constexpr float kPi = 3.14159265f;

struct Pen {
    ImDrawList* dl;
    ImVec2 o;
    float s;
    float alpha;

    ImVec2 p(float x, float y) const { return ImVec2(o.x + x * s, o.y + y * s); }
    ImU32 c(int r, int g, int b, int a = 255) const { return IM_COL32(r, g, b, (int)((float)a * alpha)); }
    float w(float px) const { return std::max(1.0f, px * s / 20.0f); }

    void line(float x0, float y0, float x1, float y1, ImU32 col, float th = 1.5f) const {
        dl->AddLine(p(x0, y0), p(x1, y1), col, w(th));
    }
    void rect(float x0, float y0, float x1, float y1, ImU32 col, float th = 1.2f) const {
        dl->AddRect(p(x0, y0), p(x1, y1), col, 0.0f, 0, w(th));
    }
    void fill(float x0, float y0, float x1, float y1, ImU32 col, float round = 0.0f) const {
        dl->AddRectFilled(p(x0, y0), p(x1, y1), col, round * s);
    }
    void circle(float x, float y, float r, ImU32 col, float th = 1.4f) const {
        dl->AddCircle(p(x, y), r * s, col, 0, w(th));
    }
    void disc(float x, float y, float r, ImU32 col) const { dl->AddCircleFilled(p(x, y), r * s, col); }
    void ellipse(float x, float y, float rx, float ry, ImU32 col, float th = 1.2f) const {
        dl->AddEllipse(p(x, y), ImVec2(rx * s, ry * s), col, 0.0f, 0, w(th));
    }
    void ellipseFill(float x, float y, float rx, float ry, ImU32 col) const {
        dl->AddEllipseFilled(p(x, y), ImVec2(rx * s, ry * s), col);
    }
    void tri(float x0, float y0, float x1, float y1, float x2, float y2, ImU32 col) const {
        dl->AddTriangleFilled(p(x0, y0), p(x1, y1), p(x2, y2), col);
    }
    void quad(float x0, float y0, float x1, float y1, float x2, float y2, float x3, float y3, ImU32 col) const {
        dl->AddQuadFilled(p(x0, y0), p(x1, y1), p(x2, y2), p(x3, y3), col);
    }
    void quadOutline(float x0, float y0, float x1, float y1, float x2, float y2, float x3, float y3, ImU32 col,
                     float th = 1.2f) const {
        dl->AddQuad(p(x0, y0), p(x1, y1), p(x2, y2), p(x3, y3), col, w(th));
    }
    void arc(float x, float y, float r, float a0, float a1, ImU32 col, float th = 1.6f) const {
        dl->PathArcTo(p(x, y), r * s, a0, a1, 24);
        dl->PathStroke(col, 0, w(th));
    }
    void bezier(float x0, float y0, float x1, float y1, float x2, float y2, float x3, float y3, ImU32 col,
                float th = 1.5f) const {
        dl->AddBezierCubic(p(x0, y0), p(x1, y1), p(x2, y2), p(x3, y3), col, w(th));
    }
    // Polilinea / poligono en coordenadas normalizadas (xy intercalados).
    void poly(const float* xy, int n, ImU32 col, float th = 1.4f, bool closed = false) const {
        ImVec2 pts[64];
        n = std::min(n, 64);
        for (int i = 0; i < n; ++i) pts[i] = p(xy[2 * i], xy[2 * i + 1]);
        dl->AddPolyline(pts, n, col, closed ? ImDrawFlags_Closed : 0, w(th));
    }
    void polyFill(const float* xy, int n, ImU32 col) const {
        ImVec2 pts[64];
        n = std::min(n, 64);
        for (int i = 0; i < n; ++i) pts[i] = p(xy[2 * i], xy[2 * i + 1]);
        dl->AddConvexPolyFilled(pts, n, col);
    }
    // Punta de flecha en (x,y) apuntando en la direccion (dx,dy).
    void head(float x, float y, float dx, float dy, float len, ImU32 col) const {
        const float n = std::sqrt(dx * dx + dy * dy);
        dx /= n;
        dy /= n;
        const float bx = x - dx * len, by = y - dy * len;
        const float px = -dy * len * 0.6f, py = dx * len * 0.6f;
        tri(x, y, bx + px, by + py, bx - px, by - py, col);
    }
    void text(float x, float y, const char* t, ImU32 col, float sizeFrac = 0.62f) const {
        ImFont* f = ImGui::GetFont();
        const float fs = s * sizeFrac;
        const ImVec2 ts = f->CalcTextSizeA(fs, FLT_MAX, 0.0f, t);
        dl->AddText(f, fs, ImVec2(p(x, y).x - ts.x * 0.5f, p(x, y).y - ts.y * 0.5f), col, t);
    }
};

// Familias de color de R19 (mismos valores que ya usaba C4DIcons.cpp).
struct Ink {
    ImU32 gray, dark, white, red, green, blue, orange, yellow;
    ImU32 blueTop, blueL, blueR, blueEdge;   // primitivas
    ImU32 splineBlue, splineDark;            // primitivas spline (trazo azul claro)
    ImU32 genGreen, genGreenDark, genGreenLight; // generadores / modelado / MoGraph
    ImU32 defViolet, defVioletDark, defVioletLight; // deformadores
    ImU32 sky, skyDark;                      // escena
    ImU32 camBody, lightWarm;                // camaras / luces
    explicit Ink(const Pen& P)
        : gray(P.c(206, 206, 206)), dark(P.c(40, 40, 40)), white(P.c(240, 240, 240)), red(P.c(225, 60, 55)),
          green(P.c(80, 190, 70)), blue(P.c(70, 110, 230)), orange(P.c(232, 150, 50)), yellow(P.c(250, 220, 120)),
          blueTop(P.c(132, 186, 240)), blueL(P.c(66, 130, 205)), blueR(P.c(44, 96, 160)), blueEdge(P.c(190, 220, 250)),
          splineBlue(P.c(120, 190, 245)), splineDark(P.c(60, 120, 190)), genGreen(P.c(100, 182, 76)),
          genGreenDark(P.c(58, 126, 46)), genGreenLight(P.c(170, 230, 150)), defViolet(P.c(140, 150, 230)),
          defVioletDark(P.c(84, 92, 176)), defVioletLight(P.c(196, 202, 250)), sky(P.c(150, 200, 240)),
          skyDark(P.c(80, 130, 190)), camBody(P.c(52, 52, 58)), lightWarm(P.c(255, 230, 140)) {}
};

inline void isoCube(const Pen& P, float cx, float cy, float r, ImU32 top, ImU32 left, ImU32 right, ImU32 edge) {
    const float h = r * 0.58f;
    P.quad(cx, cy - r, cx + r, cy - r + h, cx, cy - r + 2 * h, cx - r, cy - r + h, top);
    P.quad(cx - r, cy - r + h, cx, cy - r + 2 * h, cx, cy + r, cx - r, cy + r - h, left);
    P.quad(cx, cy - r + 2 * h, cx + r, cy - r + h, cx + r, cy + r - h, cx, cy + r, right);
    P.quadOutline(cx, cy - r, cx + r, cy - r + h, cx, cy - r + 2 * h, cx - r, cy - r + h, edge, 0.8f);
}

inline void wireCube(const Pen& P, ImU32 col) {
    P.quadOutline(0.18f, 0.35f, 0.62f, 0.35f, 0.62f, 0.82f, 0.18f, 0.82f, col, 1.1f);
    P.quadOutline(0.38f, 0.18f, 0.82f, 0.18f, 0.82f, 0.65f, 0.38f, 0.65f, col, 1.1f);
    P.line(0.18f, 0.35f, 0.38f, 0.18f, col, 1.1f);
    P.line(0.62f, 0.35f, 0.82f, 0.18f, col, 1.1f);
    P.line(0.62f, 0.82f, 0.82f, 0.65f, col, 1.1f);
    P.line(0.18f, 0.82f, 0.38f, 0.65f, col, 1.1f);
}

inline void clapper(const Pen& P) {
    const ImU32 body = P.c(52, 52, 56);
    const ImU32 edge = P.c(200, 200, 200);
    P.fill(0.15f, 0.40f, 0.85f, 0.85f, body, 0.04f);
    P.rect(0.15f, 0.40f, 0.85f, 0.85f, edge, 1.0f);
    P.quad(0.13f, 0.22f, 0.83f, 0.10f, 0.86f, 0.28f, 0.16f, 0.40f, body);
    for (int i = 0; i < 4; ++i) {
        const float t0 = 0.18f + i * 0.17f;
        P.quad(t0, 0.20f - i * 0.03f, t0 + 0.08f, 0.19f - i * 0.03f, t0 + 0.11f, 0.36f - i * 0.03f, t0 + 0.03f,
               0.37f - i * 0.03f, P.c(225, 225, 225));
    }
}

inline void gear(const Pen& P, float cx, float cy, float r, ImU32 col) {
    for (int i = 0; i < 8; ++i) {
        const float a = i * kPi / 4.0f;
        P.line(cx + std::cos(a) * r * 0.6f, cy + std::sin(a) * r * 0.6f, cx + std::cos(a) * r, cy + std::sin(a) * r,
               col, 2.4f);
    }
    P.disc(cx, cy, r * 0.68f, col);
    P.disc(cx, cy, r * 0.28f, P.c(52, 52, 56));
}

// Camara de cine base (cuerpo + 2 bobinas + objetivo), compartida por la
// familia de camaras y algun icono de menu.
inline void movieCamera(const Pen& P, float ox = 0.0f, float oy = 0.0f, float k = 1.0f) {
    auto X = [&](float x) { return ox + x * k; };
    auto Y = [&](float y) { return oy + y * k; };
    P.fill(X(0.10f), Y(0.36f), X(0.64f), Y(0.78f), P.c(60, 60, 66), 0.05f * k);
    P.rect(X(0.10f), Y(0.36f), X(0.64f), Y(0.78f), P.c(210, 210, 210), 1.0f);
    P.tri(X(0.64f), Y(0.57f), X(0.90f), Y(0.38f), X(0.90f), Y(0.76f), P.c(60, 60, 66));
    P.disc(X(0.25f), Y(0.24f), 0.12f * k, P.c(225, 225, 225));
    P.disc(X(0.25f), Y(0.24f), 0.04f * k, P.c(60, 60, 66));
    P.disc(X(0.50f), Y(0.24f), 0.12f * k, P.c(225, 225, 225));
    P.disc(X(0.50f), Y(0.24f), 0.04f * k, P.c(60, 60, 66));
}

// Bombilla base de la familia de luces.
inline void bulb(const Pen& P, float cx = 0.5f, float cy = 0.42f, float r = 0.28f) {
    P.disc(cx, cy, r, P.c(245, 245, 235));
    P.circle(cx, cy, r, P.c(160, 160, 150), 1.0f);
    P.fill(cx - r * 0.42f, cy + r * 0.95f, cx + r * 0.42f, cy + r * 1.6f, P.c(150, 150, 150), 0.03f);
    P.disc(cx, cy, r * 0.4f, P.c(255, 230, 140));
}

// Cada familia devuelve true si `icon` es suyo y lo ha dibujado.
bool drawSplineIcon(const Pen& P, Icon icon);
bool drawGeneratorIcon(const Pen& P, Icon icon);
bool drawDeformerIcon(const Pen& P, Icon icon);
bool drawSceneIcon(const Pen& P, Icon icon);
bool drawMenuIcon(const Pen& P, Icon icon);

} // namespace iron3d::icons::detail
