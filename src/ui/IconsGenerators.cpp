// Generadores (verdes), grupo Modelado y MoGraph.

#include "IconPen.h"

namespace iron3d::icons::detail {

namespace {

// Cubito isometrico de un tono (caras clara/media/oscura derivadas de el).
void smallCube(const Pen& P, float cx, float cy, float r, int R, int G, int B, int a = 255) {
    isoCube(P, cx, cy, r, P.c(std::min(255, R + 60), std::min(255, G + 50), std::min(255, B + 50), a),
            P.c(R, G, B, a), P.c(R * 2 / 3, G * 2 / 3, B * 2 / 3, a), P.c(std::min(255, R + 90), 255, 230, a));
}

} // namespace

bool drawGeneratorIcon(const Pen& P, Icon icon) {
    const Ink k(P);
    const ImU32 mint = P.c(110, 210, 150), mintDark = P.c(52, 140, 90), mintLight = P.c(190, 245, 210);
    const ImU32 teal = P.c(70, 200, 150), tealDark = P.c(36, 120, 92), tealLight = P.c(160, 240, 210);
    switch (icon) {
        // --- Generadores ------------------------------------------------------
        case Icon::GenSubdiv:
            P.fill(0.18f, 0.22f, 0.82f, 0.86f, k.genGreen, 0.16f);
            P.ellipseFill(0.50f, 0.38f, 0.22f, 0.10f, k.genGreenLight);
            P.quadOutline(0.10f, 0.14f, 0.90f, 0.14f, 0.90f, 0.92f, 0.10f, 0.92f, k.genGreenDark, 1.1f);
            P.disc(0.10f, 0.14f, 0.045f, k.genGreenDark);
            P.disc(0.90f, 0.14f, 0.045f, k.genGreenDark);
            P.disc(0.90f, 0.92f, 0.045f, k.genGreenDark);
            P.disc(0.10f, 0.92f, 0.045f, k.genGreenDark);
            return true;
        case Icon::GenExtrude:
            P.quad(0.14f, 0.40f, 0.52f, 0.40f, 0.86f, 0.18f, 0.48f, 0.18f, k.genGreenLight);
            P.quad(0.14f, 0.40f, 0.52f, 0.40f, 0.52f, 0.86f, 0.14f, 0.86f, k.genGreen);
            P.quad(0.52f, 0.40f, 0.86f, 0.18f, 0.86f, 0.64f, 0.52f, 0.86f, k.genGreenDark);
            P.head(0.80f, 0.06f, 1.0f, -0.6f, 0.14f, k.white);
            return true;
        case Icon::GenLathe: {
            const float body[] = {0.36f, 0.10f, 0.64f, 0.10f, 0.60f, 0.30f, 0.76f, 0.56f,
                                  0.64f, 0.86f, 0.36f, 0.86f, 0.24f, 0.56f, 0.40f, 0.30f};
            P.polyFill(body, 8, k.genGreen);
            P.poly(body, 8, k.genGreenDark, 1.0f, true);
            P.ellipseFill(0.50f, 0.10f, 0.14f, 0.04f, k.genGreenLight);
            P.line(0.42f, 0.40f, 0.36f, 0.70f, k.genGreenLight, 1.6f);
            return true;
        }
        case Icon::GenLoft:
            P.quad(0.12f, 0.30f, 0.58f, 0.14f, 0.88f, 0.62f, 0.40f, 0.86f, k.genGreen);
            P.bezier(0.12f, 0.30f, 0.30f, 0.50f, 0.25f, 0.70f, 0.40f, 0.86f, k.genGreenDark, 1.4f);
            P.bezier(0.58f, 0.14f, 0.74f, 0.32f, 0.70f, 0.50f, 0.88f, 0.62f, k.genGreenDark, 1.4f);
            P.bezier(0.35f, 0.22f, 0.52f, 0.40f, 0.48f, 0.60f, 0.64f, 0.74f, k.genGreenLight, 1.0f);
            return true;
        case Icon::GenSweep:
            P.bezier(0.12f, 0.78f, 0.30f, 0.20f, 0.70f, 0.80f, 0.88f, 0.22f, k.genGreenDark, 6.0f);
            P.bezier(0.12f, 0.78f, 0.30f, 0.20f, 0.70f, 0.80f, 0.88f, 0.22f, k.genGreen, 4.4f);
            P.bezier(0.12f, 0.76f, 0.30f, 0.18f, 0.70f, 0.78f, 0.88f, 0.20f, k.genGreenLight, 1.0f);
            P.ellipseFill(0.88f, 0.22f, 0.07f, 0.09f, k.genGreenLight);
            return true;
        case Icon::GenBezier:
            P.quad(0.12f, 0.70f, 0.40f, 0.20f, 0.90f, 0.30f, 0.62f, 0.86f, k.genGreen);
            for (int i = 1; i < 3; ++i) {
                const float t = (float)i / 3.0f;
                P.line(0.12f + 0.28f * t, 0.70f - 0.50f * t, 0.62f + 0.28f * t, 0.86f - 0.56f * t, k.genGreenDark, 0.9f);
                P.line(0.12f + 0.50f * t, 0.70f + 0.16f * t, 0.40f + 0.50f * t, 0.20f + 0.10f * t, k.genGreenDark, 0.9f);
            }
            P.quadOutline(0.12f, 0.70f, 0.40f, 0.20f, 0.90f, 0.30f, 0.62f, 0.86f, k.genGreenLight, 1.0f);
            return true;

        // --- Modelado ---------------------------------------------------------
        case Icon::ModArray:
            P.circle(0.5f, 0.5f, 0.32f, mintLight, 0.8f);
            for (int i = 0; i < 8; ++i) {
                const float a = (float)i * kPi / 4.0f;
                P.disc(0.5f + std::cos(a) * 0.32f, 0.5f + std::sin(a) * 0.32f, 0.085f, (i % 2) ? mint : mintDark);
            }
            return true;
        case Icon::ModAtomArray: {
            const float v[] = {0.50f, 0.14f, 0.16f, 0.80f, 0.84f, 0.80f, 0.56f, 0.66f};
            for (int a = 0; a < 4; ++a)
                for (int b = a + 1; b < 4; ++b) P.line(v[2 * a], v[2 * a + 1], v[2 * b], v[2 * b + 1], mintDark, 1.8f);
            for (int a = 0; a < 4; ++a) P.disc(v[2 * a], v[2 * a + 1], 0.09f, mint);
            return true;
        }
        case Icon::ModBoole:
            P.fill(0.12f, 0.30f, 0.62f, 0.80f, mint, 0.04f);
            P.disc(0.62f, 0.44f, 0.27f, k.white);
            P.fill(0.35f, 0.30f, 0.62f, 0.71f, mintLight);
            P.rect(0.12f, 0.30f, 0.62f, 0.80f, mintDark, 1.0f);
            P.circle(0.62f, 0.44f, 0.27f, mintDark, 1.0f);
            return true;
        case Icon::ModSplineMask:
            P.rect(0.12f, 0.36f, 0.60f, 0.84f, mint, 2.2f);
            P.circle(0.62f, 0.38f, 0.24f, k.white, 2.2f);
            return true;
        case Icon::ModConnect:
            P.dl->PathArcTo(P.p(0.42f, 0.5f), 0.32f * P.s, 0.5f * kPi, 1.5f * kPi, 20);
            P.dl->PathFillConvex(mint);
            P.dl->PathArcTo(P.p(0.58f, 0.5f), 0.32f * P.s, -0.5f * kPi, 0.5f * kPi, 20);
            P.dl->PathFillConvex(k.white);
            P.fill(0.42f, 0.42f, 0.58f, 0.58f, mintDark);
            return true;
        case Icon::ModSymmetry:
            P.dl->PathArcTo(P.p(0.5f, 0.5f), 0.36f * P.s, 0.5f * kPi, 1.5f * kPi, 24);
            P.dl->PathFillConvex(mint);
            P.dl->PathArcTo(P.p(0.5f, 0.5f), 0.36f * P.s, -0.5f * kPi, 0.5f * kPi, 24);
            P.dl->PathFillConvex(k.white);
            P.line(0.5f, 0.08f, 0.5f, 0.92f, mintDark, 1.2f);
            return true;
        case Icon::ModInstance:
            smallCube(P, 0.36f, 0.62f, 0.22f, 52, 140, 90, 170);
            smallCube(P, 0.50f, 0.50f, 0.22f, 80, 170, 110, 210);
            smallCube(P, 0.64f, 0.38f, 0.22f, 110, 210, 150);
            return true;
        case Icon::ModMetaball:
            P.disc(0.36f, 0.56f, 0.24f, mint);
            P.disc(0.66f, 0.40f, 0.20f, mint);
            P.quad(0.36f, 0.34f, 0.62f, 0.22f, 0.70f, 0.58f, 0.44f, 0.76f, mint);
            P.disc(0.30f, 0.48f, 0.07f, mintLight);
            P.disc(0.62f, 0.33f, 0.05f, mintLight);
            return true;
        case Icon::ModPython:
            P.fill(0.20f, 0.14f, 0.62f, 0.50f, P.c(70, 120, 190), 0.12f);
            P.fill(0.38f, 0.50f, 0.80f, 0.86f, P.c(240, 200, 70), 0.12f);
            P.fill(0.20f, 0.36f, 0.50f, 0.50f, P.c(70, 120, 190));
            P.fill(0.50f, 0.50f, 0.80f, 0.64f, P.c(240, 200, 70));
            P.disc(0.32f, 0.24f, 0.04f, k.white);
            P.disc(0.68f, 0.76f, 0.04f, k.white);
            return true;
        case Icon::ModLod: {
            const float leaf[] = {0.50f, 0.08f, 0.62f, 0.32f, 0.86f, 0.26f, 0.76f, 0.50f, 0.62f,
                                  0.62f, 0.38f, 0.62f, 0.24f, 0.50f, 0.14f, 0.26f, 0.38f, 0.32f};
            P.tri(0.50f, 0.08f, 0.62f, 0.62f, 0.38f, 0.62f, mint);
            P.tri(0.62f, 0.32f, 0.86f, 0.26f, 0.62f, 0.62f, mint);
            P.tri(0.86f, 0.26f, 0.76f, 0.50f, 0.62f, 0.62f, mint);
            P.tri(0.38f, 0.32f, 0.14f, 0.26f, 0.38f, 0.62f, mint);
            P.tri(0.14f, 0.26f, 0.24f, 0.50f, 0.38f, 0.62f, mint);
            P.poly(leaf, 9, mintDark, 1.0f, true);
            P.line(0.50f, 0.24f, 0.50f, 0.92f, mintDark, 1.6f);
            return true;
        }
        case Icon::ModPolyReduction:
            P.tri(0.10f, 0.84f, 0.42f, 0.24f, 0.74f, 0.84f, mint);
            P.line(0.42f, 0.24f, 0.42f, 0.84f, mintDark, 1.0f);
            P.line(0.84f, 0.16f, 0.84f, 0.62f, k.white, 2.0f);
            P.head(0.84f, 0.80f, 0.0f, 1.0f, 0.18f, k.white);
            return true;

        // --- MoGraph ----------------------------------------------------------
        case Icon::MoCloner:
            for (int y = 0; y < 3; ++y)
                for (int x = 0; x < 3; ++x)
                    smallCube(P, 0.22f + (float)x * 0.28f, 0.22f + (float)y * 0.28f, 0.11f, 70, 200, 150);
            return true;
        case Icon::MoMatrix:
            for (int y = 0; y < 3; ++y)
                for (int x = 0; x < 3; ++x) {
                    const float x0 = 0.12f + (float)x * 0.27f, y0 = 0.12f + (float)y * 0.27f;
                    P.rect(x0, y0, x0 + 0.18f, y0 + 0.18f, teal, 1.2f);
                }
            return true;
        case Icon::MoFracture:
            P.quad(0.10f, 0.30f, 0.44f, 0.30f, 0.40f, 0.86f, 0.10f, 0.86f, teal);
            P.quad(0.50f, 0.14f, 0.90f, 0.14f, 0.90f, 0.50f, 0.54f, 0.44f, tealLight);
            P.quad(0.52f, 0.52f, 0.90f, 0.58f, 0.90f, 0.86f, 0.48f, 0.86f, tealDark);
            P.tri(0.12f, 0.24f, 0.44f, 0.24f, 0.40f, 0.10f, tealLight);
            return true;
        case Icon::MoVoronoi:
            P.fill(0.12f, 0.12f, 0.88f, 0.88f, teal, 0.04f);
            P.line(0.12f, 0.40f, 0.42f, 0.46f, tealDark, 1.4f);
            P.line(0.42f, 0.46f, 0.56f, 0.12f, tealDark, 1.4f);
            P.line(0.42f, 0.46f, 0.36f, 0.88f, tealDark, 1.4f);
            P.line(0.42f, 0.46f, 0.70f, 0.62f, tealDark, 1.4f);
            P.line(0.70f, 0.62f, 0.88f, 0.42f, tealDark, 1.4f);
            P.line(0.70f, 0.62f, 0.74f, 0.88f, tealDark, 1.4f);
            P.rect(0.12f, 0.12f, 0.88f, 0.88f, tealLight, 1.0f);
            return true;
        case Icon::MoInstance:
            smallCube(P, 0.22f, 0.74f, 0.12f, 70, 200, 150, 70);
            smallCube(P, 0.40f, 0.58f, 0.14f, 70, 200, 150, 130);
            smallCube(P, 0.64f, 0.38f, 0.20f, 70, 200, 150);
            return true;
        case Icon::MoText:
            P.fill(0.20f, 0.20f, 0.88f, 0.36f, tealDark);
            P.fill(0.46f, 0.36f, 0.62f, 0.90f, tealDark);
            P.fill(0.12f, 0.12f, 0.80f, 0.28f, teal);
            P.fill(0.38f, 0.28f, 0.54f, 0.82f, teal);
            return true;
        case Icon::MoTracer:
            P.bezier(0.10f, 0.80f, 0.30f, 0.40f, 0.50f, 0.70f, 0.78f, 0.22f, tealLight, 1.2f);
            P.bezier(0.10f, 0.56f, 0.30f, 0.20f, 0.56f, 0.46f, 0.86f, 0.12f, tealLight, 1.2f);
            P.disc(0.78f, 0.22f, 0.09f, teal);
            P.disc(0.86f, 0.12f, 0.07f, teal);
            return true;
        case Icon::MoSpline: {
            float pts[48];
            for (int i = 0; i < 24; ++i) {
                const float t = (float)i / 23.0f;
                const float a = t * 4.0f * kPi;
                const float r = 0.06f + t * 0.34f;
                pts[2 * i] = 0.5f + std::cos(a) * r;
                pts[2 * i + 1] = 0.5f + std::sin(a) * r;
            }
            P.poly(pts, 24, teal, 2.0f);
            return true;
        }
        case Icon::MoExtrude:
            P.fill(0.10f, 0.66f, 0.90f, 0.88f, tealDark);
            P.fill(0.22f, 0.46f, 0.78f, 0.66f, teal);
            P.fill(0.34f, 0.26f, 0.66f, 0.46f, tealLight);
            P.fill(0.44f, 0.10f, 0.56f, 0.26f, k.white);
            return true;
        case Icon::MoPolyFx:
            P.tri(0.10f, 0.40f, 0.36f, 0.14f, 0.40f, 0.46f, teal);
            P.tri(0.52f, 0.18f, 0.88f, 0.24f, 0.66f, 0.46f, tealLight);
            P.tri(0.14f, 0.86f, 0.30f, 0.56f, 0.50f, 0.80f, tealDark);
            P.tri(0.56f, 0.60f, 0.90f, 0.66f, 0.70f, 0.90f, teal);
            return true;
        default:
            return false;
    }
}

} // namespace iron3d::icons::detail
