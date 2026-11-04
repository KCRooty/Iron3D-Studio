// Splines: herramientas de dibujo (pluma naranja/negra), primitivas spline (trazo azul claro) y operaciones booleanas (gris).

#include "IconPen.h"

namespace iron3d::icons::detail {

namespace {

// Plumin estilografico con la punta en (tx,ty), apuntando hacia arriba a la
// derecha; `len` = largo en unidades del icono.
void nib(const Pen& P, float tx, float ty, float len, ImU32 body, ImU32 edge) {
    const float dx = 0.7071f, dy = -0.7071f, nx = 0.7071f, ny = 0.7071f;
    auto pt = [&](float a, float b, float* o) {
        o[0] = tx + dx * a * len + nx * b * len;
        o[1] = ty + dy * a * len + ny * b * len;
    };
    float xy[10];
    pt(0.0f, 0.0f, xy);
    pt(0.45f, -0.17f, xy + 2);
    pt(1.0f, -0.12f, xy + 4);
    pt(1.0f, 0.12f, xy + 6);
    pt(0.45f, 0.17f, xy + 8);
    P.polyFill(xy, 5, body);
    P.poly(xy, 5, edge, 1.0f, true);
    P.line(tx, ty, tx + dx * 0.40f * len, ty + dy * 0.40f * len, edge, 1.0f);
    P.disc(tx + dx * 0.45f * len, ty + dy * 0.45f * len, 0.05f * len, edge);
}

// Dos cuadrados redondeados solapados (A arriba-izquierda, B abajo-derecha)
// de los iconos de operaciones booleanas entre splines.
constexpr float kAx0 = 0.10f, kAy0 = 0.14f, kAx1 = 0.62f, kAy1 = 0.66f;
constexpr float kBx0 = 0.38f, kBy0 = 0.34f, kBx1 = 0.90f, kBy1 = 0.86f;

void boolOutlines(const Pen& P, ImU32 col) {
    P.dl->AddRect(P.p(kAx0, kAy0), P.p(kAx1, kAy1), col, 0.12f * P.s, 0, P.w(1.2f));
    P.dl->AddRect(P.p(kBx0, kBy0), P.p(kBx1, kBy1), col, 0.12f * P.s, 0, P.w(1.2f));
}

} // namespace

bool drawSplineIcon(const Pen& P, Icon icon) {
    const Ink k(P);
    const ImU32 b = k.splineBlue;
    const float th = 1.8f;
    const ImU32 nibEdge = P.c(30, 30, 30);
    const ImU32 boolLight = P.c(175, 175, 180);
    const ImU32 boolMid = P.c(130, 130, 136);
    const ImU32 boolDark = P.c(45, 45, 48);
    const ImU32 boolEdge = P.c(225, 225, 225);

    switch (icon) {
        // --- Herramientas ---
        case Icon::SplPen:
            nib(P, 0.14f, 0.86f, 0.80f, k.orange, nibEdge);
            return true;
        case Icon::SplSketch:
            P.disc(0.5f, 0.5f, 0.40f, k.orange);
            P.circle(0.5f, 0.5f, 0.40f, nibEdge, 1.0f);
            nib(P, 0.30f, 0.70f, 0.50f, P.c(245, 245, 240), nibEdge);
            return true;
        case Icon::SplSmooth: {
            P.fill(0.10f, 0.66f, 0.88f, 0.76f, P.c(70, 70, 74), 0.02f);
            P.quad(0.14f, 0.66f, 0.84f, 0.66f, 0.74f, 0.42f, 0.34f, 0.40f, k.orange);
            P.quadOutline(0.14f, 0.66f, 0.84f, 0.66f, 0.74f, 0.42f, 0.34f, 0.40f, nibEdge, 0.9f);
            const float hx[] = {0.40f, 0.40f, 0.44f, 0.22f, 0.70f, 0.22f, 0.72f, 0.42f};
            P.poly(hx, 4, nibEdge, 2.0f);
            P.bezier(0.08f, 0.88f, 0.30f, 0.80f, 0.60f, 0.96f, 0.92f, 0.86f, b, 1.2f);
            return true;
        }
        case Icon::SplArcTool:
            P.arc(0.62f, 0.62f, 0.46f, 0.95f * kPi, 1.45f * kPi, b, th);
            P.disc(0.62f + std::cos(0.95f * kPi) * 0.46f, 0.62f + std::sin(0.95f * kPi) * 0.46f, 0.05f, k.white);
            P.disc(0.62f + std::cos(1.45f * kPi) * 0.46f, 0.62f + std::sin(1.45f * kPi) * 0.46f, 0.05f, k.white);
            nib(P, 0.44f, 0.86f, 0.56f, k.orange, nibEdge);
            return true;

        // --- Primitivas spline ---
        case Icon::SplArc:
            P.arc(0.82f, 0.82f, 0.66f, 1.0f * kPi, 1.5f * kPi, b, th);
            return true;
        case Icon::SplCircle:
            P.circle(0.5f, 0.5f, 0.36f, b, th);
            return true;
        case Icon::SplHelix:
            for (int i = 0; i < 4; ++i) P.ellipse(0.5f, 0.22f + i * 0.19f, 0.32f, 0.09f, b, 1.4f);
            return true;
        case Icon::SplNSide: {
            float xy[12];
            for (int i = 0; i < 6; ++i) {
                const float a = i * kPi / 3.0f + kPi / 6.0f;
                xy[2 * i] = 0.5f + std::cos(a) * 0.38f;
                xy[2 * i + 1] = 0.5f + std::sin(a) * 0.38f;
            }
            P.poly(xy, 6, b, th, true);
            return true;
        }
        case Icon::SplRectangle:
            P.rect(0.16f, 0.22f, 0.84f, 0.78f, b, th);
            return true;
        case Icon::SplStar: {
            float xy[20];
            for (int i = 0; i < 10; ++i) {
                const float a = -0.5f * kPi + i * kPi / 5.0f;
                const float r = (i & 1) ? 0.17f : 0.40f;
                xy[2 * i] = 0.5f + std::cos(a) * r;
                xy[2 * i + 1] = 0.54f + std::sin(a) * r;
            }
            P.poly(xy, 10, b, 1.6f, true);
            return true;
        }
        case Icon::SplText: {
            const float t[] = {0.18f, 0.18f, 0.82f, 0.18f, 0.82f, 0.30f, 0.58f, 0.30f, 0.58f, 0.84f,
                               0.42f, 0.84f, 0.42f, 0.30f, 0.18f, 0.30f};
            P.poly(t, 8, b, 1.5f, true);
            return true;
        }
        case Icon::SplVectorizer:
            P.dl->PathArcTo(P.p(0.5f, 0.5f), 0.36f * P.s, 0.0f, kPi, 24);
            P.dl->PathFillConvex(k.white);
            P.circle(0.5f, 0.5f, 0.36f, b, th);
            P.line(0.14f, 0.5f, 0.86f, 0.5f, b, 1.2f);
            return true;
        case Icon::SplFourSide: {
            const float xy[] = {0.5f, 0.14f, 0.86f, 0.5f, 0.5f, 0.86f, 0.14f, 0.5f};
            P.poly(xy, 4, b, th, true);
            return true;
        }
        case Icon::SplCissoid:
            P.bezier(0.12f, 0.16f, 0.16f, 0.52f, 0.36f, 0.76f, 0.5f, 0.84f, b, th);
            P.bezier(0.88f, 0.16f, 0.84f, 0.52f, 0.64f, 0.76f, 0.5f, 0.84f, b, th);
            return true;
        case Icon::SplCog: {
            float xy[64];
            const int teeth = 8;
            const float w = kPi / teeth;
            int n = 0;
            for (int i = 0; i < teeth; ++i) {
                const float a = i * 2.0f * kPi / teeth;
                const float as[] = {a - w * 0.9f, a - w * 0.45f, a + w * 0.45f, a + w * 0.9f};
                const float rs[] = {0.27f, 0.40f, 0.40f, 0.27f};
                for (int j = 0; j < 4; ++j) {
                    xy[2 * n] = 0.5f + std::cos(as[j]) * rs[j];
                    xy[2 * n + 1] = 0.5f + std::sin(as[j]) * rs[j];
                    ++n;
                }
            }
            P.poly(xy, n, b, 1.4f, true);
            P.circle(0.5f, 0.5f, 0.10f, b, 1.3f);
            return true;
        }
        case Icon::SplCycloid: {
            float xy[80];
            const int n = 40;
            for (int i = 0; i < n; ++i) {
                const float t = 4.0f * kPi * (float)i / (float)(n - 1);
                xy[2 * i] = 0.10f + (t - std::sin(t)) * (0.80f / (4.0f * kPi));
                xy[2 * i + 1] = 0.74f - (1.0f - std::cos(t)) * 0.20f;
            }
            P.poly(xy, n, b, th);
            P.line(0.08f, 0.80f, 0.92f, 0.80f, P.c(120, 190, 245, 110), 1.0f);
            return true;
        }
        case Icon::SplFormula: {
            float xy[60];
            const int n = 30;
            for (int i = 0; i < n; ++i) {
                const float t = (float)i / (float)(n - 1);
                xy[2 * i] = 0.10f + t * 0.80f;
                xy[2 * i + 1] = 0.5f - std::sin(t * 2.0f * kPi) * 0.30f;
            }
            P.poly(xy, n, b, th);
            return true;
        }
        case Icon::SplFlower: {
            float xy[120];
            const int n = 60;
            for (int i = 0; i < n; ++i) {
                const float a = -0.5f * kPi + 2.0f * kPi * (float)i / (float)n;
                const float r = 0.25f + 0.14f * std::cos(5.0f * (a + 0.5f * kPi));
                xy[2 * i] = 0.5f + std::cos(a) * r;
                xy[2 * i + 1] = 0.5f + std::sin(a) * r;
            }
            P.poly(xy, n, b, 1.5f, true);
            return true;
        }
        case Icon::SplProfile: {
            const float xy[] = {0.18f, 0.16f, 0.82f, 0.16f, 0.82f, 0.30f, 0.58f, 0.30f, 0.58f, 0.70f, 0.82f, 0.70f,
                                0.82f, 0.84f, 0.18f, 0.84f, 0.18f, 0.70f, 0.42f, 0.70f, 0.42f, 0.30f, 0.18f, 0.30f};
            P.poly(xy, 12, b, 1.5f, true);
            return true;
        }

        // --- Operaciones booleanas ---
        case Icon::SplSubtract:
            P.fill(kAx0, kAy0, kAx1, kAy1, boolLight, 0.12f);
            P.fill(kBx0, kBy0, kBx1, kBy1, boolDark, 0.12f);
            P.dl->AddRect(P.p(kBx0, kBy0), P.p(kBx1, kBy1), boolMid, 0.12f * P.s, 0, P.w(1.0f));
            P.dl->AddRect(P.p(kAx0, kAy0), P.p(kAx1, kAy1), boolEdge, 0.12f * P.s, 0, P.w(1.0f));
            return true;
        case Icon::SplUnion:
            P.fill(kAx0, kAy0, kAx1, kAy1, boolLight, 0.12f);
            P.fill(kBx0, kBy0, kBx1, kBy1, boolLight, 0.12f);
            P.line(kAx1 - 0.02f, kAy0 + 0.06f, kAx1 - 0.02f, kBy0, boolEdge, 1.0f);
            P.line(kAx0 + 0.06f, kAy1 - 0.02f, kBx0, kAy1 - 0.02f, boolEdge, 1.0f);
            return true;
        case Icon::SplAnd:
            boolOutlines(P, boolMid);
            P.fill(kBx0, kBy0, kAx1, kAy1, boolLight);
            P.rect(kBx0, kBy0, kAx1, kAy1, boolEdge, 1.2f);
            return true;
        case Icon::SplOr:
            P.fill(kAx0, kAy0, kAx1, kAy1, boolLight, 0.12f);
            P.fill(kBx0, kBy0, kBx1, kBy1, boolLight, 0.12f);
            P.fill(kBx0, kBy0, kAx1, kAy1, boolDark);
            P.rect(kBx0, kBy0, kAx1, kAy1, boolMid, 1.0f);
            return true;
        case Icon::SplIntersect:
            P.fill(kAx0, kAy0, kAx1, kAy1, boolMid, 0.12f);
            P.fill(kBx0, kBy0, kBx1, kBy1, P.c(95, 95, 100), 0.12f);
            P.fill(kBx0, kBy0, kAx1, kAy1, boolLight);
            boolOutlines(P, boolEdge);
            P.disc(kAx1, kBy0, 0.05f, k.orange);
            P.disc(kBx0, kAy1, 0.05f, k.orange);
            return true;

        default:
            return false;
    }
}

} // namespace iron3d::icons::detail
