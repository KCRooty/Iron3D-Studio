// Deformadores (familia lila-azul): casi todos son una columna o caja
// deformada que ensena el efecto del deformador.

#include "IconPen.h"

namespace iron3d::icons::detail {

namespace {

// Columna deformada: `cx(t)` centro y `hw(t)` semiancho para t en [0,1]
// (t=0 arriba). Cara iluminada a la izquierda, sombra a la derecha.
template <class C, class W>
void column(const Pen& P, const Ink& k, C cx, W hw, float y0 = 0.12f, float y1 = 0.88f, int n = 12) {
    for (int i = 0; i < n; ++i) {
        const float t0 = (float)i / (float)n, t1 = (float)(i + 1) / (float)n;
        const float ya = y0 + (y1 - y0) * t0, yb = y0 + (y1 - y0) * t1;
        const float xa = cx(t0), xb = cx(t1), wa = hw(t0), wb = hw(t1);
        P.quad(xa - wa, ya, xa, ya, xb, yb, xb - wb, yb, k.defViolet);
        P.quad(xa, ya, xa + wa, ya, xb + wb, yb, xb, yb, k.defVioletDark);
    }
    P.ellipseFill(cx(0.0f), y0, hw(0.0f), hw(0.0f) * 0.32f, k.defVioletLight);
}

void box(const Pen& P, const Ink& k, float cx = 0.5f, float cy = 0.52f, float r = 0.34f) {
    isoCube(P, cx, cy, r, k.defVioletLight, k.defViolet, k.defVioletDark, k.defVioletLight);
}

void ball(const Pen& P, const Ink& k, float x, float y, float r) {
    P.disc(x, y, r, k.defVioletDark);
    P.disc(x - r * 0.18f, y - r * 0.18f, r * 0.78f, k.defViolet);
    P.disc(x - r * 0.38f, y - r * 0.38f, r * 0.25f, k.defVioletLight);
}

void sheet(const Pen& P, const Ink& /*k*/, float y, float amp, float freq, ImU32 top, ImU32 front) {
    constexpr int n = 12;
    for (int i = 0; i < n; ++i) {
        const float x0 = 0.10f + 0.80f * (float)i / n, x1 = 0.10f + 0.80f * (float)(i + 1) / n;
        const float y0 = y + std::sin(x0 * freq) * amp, y1 = y + std::sin(x1 * freq) * amp;
        P.quad(x0, y0 - 0.14f, x1, y1 - 0.14f, x1, y1, x0, y0, top);
        P.quad(x0, y0, x1, y1, x1, y1 + 0.08f, x0, y0 + 0.08f, front);
    }
}

} // namespace

bool drawDeformerIcon(const Pen& P, Icon icon) {
    const Ink k(P);
    switch (icon) {
        case Icon::DefBend:
            column(P, k, [](float t) { return 0.40f + 0.22f * t * t; }, [](float) { return 0.14f; });
            return true;
        case Icon::DefBulge:
            column(P, k, [](float) { return 0.5f; }, [](float t) { return 0.13f + 0.17f * std::sin(t * kPi); });
            return true;
        case Icon::DefShear:
            P.quad(0.36f, 0.14f, 0.86f, 0.14f, 0.64f, 0.86f, 0.14f, 0.86f, k.defViolet);
            P.quad(0.61f, 0.14f, 0.86f, 0.14f, 0.64f, 0.86f, 0.39f, 0.86f, k.defVioletDark);
            P.quadOutline(0.36f, 0.14f, 0.86f, 0.14f, 0.64f, 0.86f, 0.14f, 0.86f, k.defVioletLight, 0.8f);
            return true;
        case Icon::DefTaper:
            column(P, k, [](float) { return 0.5f; }, [](float t) { return 0.08f + 0.26f * t; });
            return true;
        case Icon::DefTwist:
            column(P, k, [](float) { return 0.5f; }, [](float) { return 0.18f; });
            for (int i = 0; i < 4; ++i) {
                const float y = 0.22f + (float)i * 0.18f;
                P.line(0.32f, y + 0.10f, 0.68f, y - 0.04f, k.defVioletLight, 1.4f);
            }
            return true;
        case Icon::DefFfd:
            box(P, k);
            wireCube(P, k.defVioletLight);
            for (float x : {0.18f, 0.62f})
                for (float y : {0.35f, 0.82f}) P.disc(x, y, 0.055f, k.white);
            for (float x : {0.38f, 0.82f})
                for (float y : {0.18f, 0.65f}) P.disc(x, y, 0.055f, k.white);
            return true;
        case Icon::DefMesh:
            ball(P, k, 0.5f, 0.52f, 0.22f);
            P.rect(0.16f, 0.18f, 0.84f, 0.86f, k.gray, 1.0f);
            P.line(0.5f, 0.18f, 0.5f, 0.86f, k.gray, 0.8f);
            P.line(0.16f, 0.52f, 0.84f, 0.52f, k.gray, 0.8f);
            return true;
        case Icon::DefSquash:
            P.fill(0.14f, 0.40f, 0.86f, 0.66f, k.defViolet, 0.04f);
            P.fill(0.50f, 0.40f, 0.86f, 0.66f, k.defVioletDark, 0.04f);
            P.line(0.5f, 0.08f, 0.5f, 0.30f, k.gray, 1.4f);
            P.head(0.5f, 0.36f, 0, 1, 0.12f, k.gray);
            P.line(0.5f, 0.92f, 0.5f, 0.76f, k.gray, 1.4f);
            P.head(0.5f, 0.70f, 0, -1, 0.12f, k.gray);
            return true;
        case Icon::DefMelt:
            P.ellipseFill(0.5f, 0.80f, 0.38f, 0.10f, k.defVioletDark);
            column(P, k, [](float) { return 0.5f; }, [](float t) { return 0.14f + 0.22f * t * t * t; }, 0.18f,
                   0.80f);
            P.disc(0.24f, 0.86f, 0.05f, k.defViolet);
            return true;
        case Icon::DefExplosion:
            P.disc(0.42f, 0.58f, 0.24f, k.camBody);
            P.disc(0.34f, 0.50f, 0.07f, k.gray);
            P.line(0.58f, 0.40f, 0.70f, 0.26f, k.orange, 1.6f);
            P.disc(0.72f, 0.24f, 0.05f, k.yellow);
            P.quad(0.78f, 0.52f, 0.88f, 0.50f, 0.90f, 0.60f, 0.80f, 0.62f, k.defViolet);
            P.quad(0.70f, 0.74f, 0.80f, 0.76f, 0.76f, 0.86f, 0.66f, 0.84f, k.defViolet);
            return true;
        case Icon::DefExplosionFx:
            box(P, k, 0.5f, 0.54f, 0.32f);
            P.text(0.5f, 0.56f, "FX", k.white, 0.42f);
            return true;
        case Icon::DefShatter:
            isoCube(P, 0.36f, 0.32f, 0.20f, k.defVioletLight, k.defViolet, k.defVioletDark, k.defVioletLight);
            P.quad(0.18f, 0.66f, 0.32f, 0.62f, 0.36f, 0.76f, 0.22f, 0.80f, k.defViolet);
            P.quad(0.44f, 0.74f, 0.56f, 0.70f, 0.58f, 0.84f, 0.46f, 0.88f, k.defVioletDark);
            P.tri(0.62f, 0.56f, 0.72f, 0.60f, 0.64f, 0.68f, k.defViolet);
            P.line(0.80f, 0.20f, 0.80f, 0.70f, k.gray, 1.6f);
            P.head(0.80f, 0.80f, 0, 1, 0.14f, k.gray);
            return true;
        case Icon::DefCorrection:
            P.fill(0.36f, 0.10f, 0.64f, 0.90f, k.defVioletDark, 0.12f);
            for (int i = 0; i < 6; ++i) {
                const float y = 0.18f + (float)i * 0.13f;
                P.disc(0.44f, y, 0.04f, k.defVioletLight);
                P.disc(0.56f, y, 0.04f, k.defVioletLight);
            }
            return true;
        case Icon::DefJiggle:
            box(P, k, 0.48f, 0.56f, 0.30f);
            P.arc(0.48f, 0.56f, 0.40f, -0.85f * kPi, -0.6f * kPi, k.gray, 1.2f);
            P.arc(0.48f, 0.56f, 0.40f, -0.4f * kPi, -0.15f * kPi, k.gray, 1.2f);
            P.arc(0.48f, 0.56f, 0.46f, -0.75f * kPi, -0.25f * kPi, k.gray, 1.0f);
            return true;
        case Icon::DefMorph:
            ball(P, k, 0.28f, 0.36f, 0.17f);
            P.ellipseFill(0.70f, 0.70f, 0.20f, 0.13f, k.defViolet);
            P.ellipseFill(0.66f, 0.66f, 0.08f, 0.04f, k.defVioletLight);
            P.line(0.40f, 0.48f, 0.52f, 0.57f, k.white, 1.6f);
            P.head(0.58f, 0.61f, 1.0f, 0.75f, 0.12f, k.white);
            return true;
        case Icon::DefShrinkWrap:
            ball(P, k, 0.30f, 0.30f, 0.15f);
            ball(P, k, 0.68f, 0.68f, 0.21f);
            P.line(0.42f, 0.42f, 0.48f, 0.48f, k.white, 1.6f);
            P.head(0.53f, 0.53f, 1, 1, 0.11f, k.white);
            return true;
        case Icon::DefSpherify:
            ball(P, k, 0.5f, 0.5f, 0.36f);
            return true;
        case Icon::DefSurface:
            P.quad(0.12f, 0.40f, 0.62f, 0.22f, 0.88f, 0.52f, 0.36f, 0.74f, k.defViolet);
            P.bezier(0.36f, 0.74f, 0.50f, 0.84f, 0.74f, 0.72f, 0.88f, 0.52f, k.defVioletDark, 3.0f);
            P.bezier(0.12f, 0.40f, 0.30f, 0.28f, 0.46f, 0.20f, 0.62f, 0.22f, k.defVioletLight, 1.4f);
            return true;
        case Icon::DefWrap:
            P.arc(0.48f, 0.80f, 0.40f, -0.92f * kPi, -0.08f * kPi, k.defVioletDark, 7.0f);
            P.arc(0.48f, 0.80f, 0.40f, -0.92f * kPi, -0.08f * kPi, k.defViolet, 3.5f);
            P.disc(0.68f, 0.34f, 0.20f, k.blueL);
            P.circle(0.68f, 0.34f, 0.20f, k.white, 1.0f);
            P.text(0.68f, 0.35f, "W", k.white, 0.32f);
            return true;
        case Icon::DefSpline:
            P.quad(0.10f, 0.62f, 0.62f, 0.62f, 0.90f, 0.86f, 0.38f, 0.86f, k.defViolet);
            P.bezier(0.10f, 0.40f, 0.30f, 0.10f, 0.60f, 0.70f, 0.90f, 0.30f, k.white, 1.8f);
            return true;
        case Icon::DefSplineRail:
            P.bezier(0.10f, 0.22f, 0.40f, 0.10f, 0.60f, 0.40f, 0.90f, 0.26f, k.splineBlue, 1.6f);
            P.bezier(0.10f, 0.78f, 0.40f, 0.66f, 0.60f, 0.92f, 0.90f, 0.80f, k.splineBlue, 1.6f);
            for (int i = 0; i < 4; ++i) {
                const float x = 0.20f + (float)i * 0.20f;
                P.line(x, 0.26f, x, 0.76f, k.defViolet, 2.6f);
            }
            return true;
        case Icon::DefSplineWrap:
            P.bezier(0.10f, 0.80f, 0.20f, 0.10f, 0.80f, 0.90f, 0.90f, 0.20f, k.splineBlue, 1.2f);
            P.bezier(0.18f, 0.74f, 0.28f, 0.16f, 0.72f, 0.84f, 0.82f, 0.26f, k.defViolet, 5.0f);
            P.bezier(0.18f, 0.74f, 0.28f, 0.16f, 0.72f, 0.84f, 0.82f, 0.26f, k.defVioletLight, 1.2f);
            return true;
        case Icon::DefCamera:
            for (int i = 0; i < 4; ++i) {
                const float t = 0.12f + (float)i * 0.25f;
                P.line(t, 0.62f, t, 0.92f, k.defViolet, 1.0f);
                P.line(0.12f, 0.62f + (float)i * 0.10f, 0.87f, 0.62f + (float)i * 0.10f, k.defViolet, 1.0f);
            }
            movieCamera(P, 0.10f, 0.0f, 0.70f);
            return true;
        case Icon::DefCollision:
            P.quad(0.08f, 0.66f, 0.30f, 0.66f, 0.40f, 0.86f, 0.08f, 0.86f, k.defViolet);
            P.quad(0.60f, 0.66f, 0.92f, 0.66f, 0.92f, 0.86f, 0.60f, 0.86f, k.defViolet);
            P.ellipseFill(0.5f, 0.74f, 0.20f, 0.10f, k.defVioletDark);
            ball(P, k, 0.5f, 0.46f, 0.22f);
            return true;
        case Icon::DefDisplacer:
            sheet(P, k, 0.66f, 0.06f, 18.0f, k.defViolet, k.defVioletDark);
            for (float x : {0.28f, 0.52f, 0.74f}) P.disc(x, 0.44f, 0.06f, k.defVioletLight);
            return true;
        case Icon::DefFormula:
            sheet(P, k, 0.62f, 0.12f, 9.0f, k.defViolet, k.defVioletDark);
            P.text(0.78f, 0.22f, "f", k.white, 0.5f);
            return true;
        case Icon::DefWind:
            P.line(0.20f, 0.10f, 0.20f, 0.90f, k.gray, 2.0f);
            P.quad(0.22f, 0.14f, 0.86f, 0.16f, 0.82f, 0.48f, 0.22f, 0.50f, k.defViolet);
            P.bezier(0.22f, 0.14f, 0.42f, 0.06f, 0.62f, 0.26f, 0.86f, 0.16f, k.defVioletLight, 1.4f);
            P.bezier(0.22f, 0.50f, 0.42f, 0.42f, 0.62f, 0.58f, 0.82f, 0.48f, k.defVioletDark, 2.2f);
            return true;
        case Icon::DefSmoothing:
            P.fill(0.16f, 0.22f, 0.84f, 0.82f, k.defVioletDark, 0.24f);
            P.fill(0.16f, 0.22f, 0.70f, 0.70f, k.defViolet, 0.22f);
            P.disc(0.34f, 0.38f, 0.08f, k.defVioletLight);
            return true;
        case Icon::DefBevel: {
            const float oct[] = {0.30f, 0.14f, 0.70f, 0.14f, 0.86f, 0.30f, 0.86f, 0.70f,
                                 0.70f, 0.86f, 0.30f, 0.86f, 0.14f, 0.70f, 0.14f, 0.30f};
            P.polyFill(oct, 8, k.defViolet);
            P.quad(0.70f, 0.14f, 0.86f, 0.30f, 0.86f, 0.70f, 0.70f, 0.86f, k.defVioletDark);
            P.quad(0.30f, 0.14f, 0.70f, 0.14f, 0.62f, 0.26f, 0.38f, 0.26f, k.defVioletLight);
            P.poly(oct, 8, k.defVioletLight, 0.8f, true);
            return true;
        }
        default:
            return false;
    }
}

} // namespace iron3d::icons::detail
