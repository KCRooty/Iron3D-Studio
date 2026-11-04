// Escena, camaras y luces.

#include "IconPen.h"

namespace iron3d::icons::detail {

namespace {

// Nube: tres lobulos sobre una base plana.
void cloud(const Pen& P, float cx, float cy, float k, ImU32 fill, ImU32 edge, bool filled) {
    if (filled) {
        P.disc(cx - 0.16f * k, cy + 0.02f * k, 0.13f * k, fill);
        P.disc(cx + 0.02f * k, cy - 0.06f * k, 0.17f * k, fill);
        P.disc(cx + 0.19f * k, cy + 0.03f * k, 0.12f * k, fill);
        P.fill(cx - 0.29f * k, cy + 0.02f * k, cx + 0.31f * k, cy + 0.15f * k, fill, 0.06f * k);
    }
    P.arc(cx - 0.16f * k, cy + 0.02f * k, 0.13f * k, 0.5f * kPi, 1.6f * kPi, edge, 1.3f);
    P.arc(cx + 0.02f * k, cy - 0.06f * k, 0.17f * k, 1.15f * kPi, 1.95f * kPi, edge, 1.3f);
    P.arc(cx + 0.19f * k, cy + 0.03f * k, 0.12f * k, 1.6f * kPi, 2.5f * kPi, edge, 1.3f);
    P.line(cx - 0.16f * k, cy + 0.15f * k, cx + 0.19f * k, cy + 0.15f * k, edge, 1.3f);
}

void targetRings(const Pen& P, float cx, float cy, float r) {
    P.disc(cx, cy, r, P.c(40, 40, 40));
    P.circle(cx, cy, r, P.c(235, 235, 235), 1.2f);
    P.circle(cx, cy, r * 0.55f, P.c(235, 235, 235), 1.1f);
    P.disc(cx, cy, r * 0.18f, P.c(235, 235, 235));
}

void sun(const Pen& P, float cx, float cy, float r, ImU32 col) {
    for (int i = 0; i < 8; ++i) {
        const float a = i * kPi / 4.0f;
        P.line(cx + std::cos(a) * r * 1.3f, cy + std::sin(a) * r * 1.3f, cx + std::cos(a) * r * 1.75f,
               cy + std::sin(a) * r * 1.75f, col, 1.6f);
    }
    P.disc(cx, cy, r, col);
}

void smallCube(const Pen& P, float cx, float cy, float r) {
    isoCube(P, cx, cy, r, P.c(235, 235, 235), P.c(185, 185, 190), P.c(140, 140, 150), P.c(250, 250, 250));
}

// Cursor de flecha (punta arriba-izquierda en x,y): triangulo + rabo.
void cursor(const Pen& P, float x, float y) {
    P.line(x + 0.10f, y + 0.16f, x + 0.22f, y + 0.34f, P.c(40, 40, 40), 3.2f);
    P.line(x + 0.10f, y + 0.16f, x + 0.22f, y + 0.34f, P.c(240, 240, 240), 1.6f);
    P.tri(x, y, x, y + 0.30f, x + 0.22f, y + 0.20f, P.c(240, 240, 240));
    P.line(x, y, x, y + 0.30f, P.c(40, 40, 40), 0.9f);
    P.line(x, y, x + 0.22f, y + 0.20f, P.c(40, 40, 40), 0.9f);
}

} // namespace

bool drawSceneIcon(const Pen& P, Icon icon) {
    const Ink k(P);
    switch (icon) {
        case Icon::SceSky:
            P.disc(0.5f, 0.5f, 0.38f, k.sky);
            P.circle(0.5f, 0.5f, 0.38f, k.skyDark, 1.2f);
            P.ellipse(0.5f, 0.5f, 0.16f, 0.38f, k.skyDark, 1.0f);
            P.line(0.12f, 0.5f, 0.88f, 0.5f, k.skyDark, 1.0f);
            P.ellipse(0.5f, 0.5f, 0.38f, 0.14f, k.skyDark, 0.9f);
            P.disc(0.38f, 0.36f, 0.07f, P.c(230, 245, 255, 200));
            return true;
        case Icon::SceEnvironment:
            P.fill(0.10f, 0.10f, 0.90f, 0.90f, k.sky, 0.06f);
            P.rect(0.10f, 0.10f, 0.90f, 0.90f, k.skyDark, 1.1f);
            smallCube(P, 0.5f, 0.52f, 0.27f);
            return true;
        case Icon::SceForeground:
            P.fill(0.30f, 0.12f, 0.92f, 0.62f, k.white, 0.03f);
            P.rect(0.30f, 0.12f, 0.92f, 0.62f, k.gray, 1.0f);
            smallCube(P, 0.30f, 0.66f, 0.21f);
            return true;
        case Icon::SceBackground:
            smallCube(P, 0.70f, 0.30f, 0.19f);
            P.fill(0.08f, 0.38f, 0.72f, 0.88f, k.skyDark, 0.03f);
            P.fill(0.11f, 0.41f, 0.69f, 0.85f, k.sky, 0.02f);
            return true;
        case Icon::SceStage:
            clapper(P);
            P.tri(0.40f, 0.50f, 0.66f, 0.63f, 0.40f, 0.76f, k.white);
            return true;
        case Icon::ScePhysicalSky:
            sun(P, 0.62f, 0.34f, 0.15f, k.yellow);
            cloud(P, 0.44f, 0.62f, 1.0f, P.c(225, 235, 245), k.skyDark, true);
            return true;
        case Icon::SceCloudTool:
            cloud(P, 0.58f, 0.32f, 0.85f, 0, k.gray, false);
            P.line(0.16f, 0.88f, 0.52f, 0.52f, P.c(160, 120, 70), 2.6f);
            P.tri(0.50f, 0.46f, 0.60f, 0.56f, 0.46f, 0.60f, k.gray);
            return true;
        case Icon::SceCloudGroup:
            cloud(P, 0.40f, 0.38f, 0.8f, 0, k.gray, false);
            cloud(P, 0.60f, 0.64f, 0.8f, 0, k.gray, false);
            return true;
        case Icon::SceCloud:
            cloud(P, 0.50f, 0.52f, 1.15f, 0, k.gray, false);
            return true;
        case Icon::SceCloudJoin:
            cloud(P, 0.44f, 0.56f, 1.0f, 0, k.gray, false);
            P.line(0.80f, 0.12f, 0.80f, 0.38f, k.gray, 2.0f);
            P.line(0.67f, 0.25f, 0.93f, 0.25f, k.gray, 2.0f);
            return true;
        case Icon::SceGrass:
            for (int i = 0; i < 6; ++i) {
                const float x = 0.14f + i * 0.14f;
                const float lean = (i % 2 ? 0.08f : -0.06f);
                P.line(x, 0.88f, x + lean, 0.30f + (i % 3) * 0.08f, k.genGreen, 1.8f);
            }
            P.line(0.08f, 0.88f, 0.92f, 0.88f, k.genGreenDark, 1.6f);
            return true;
        case Icon::CamTarget:
            movieCamera(P, -0.04f, -0.06f, 0.92f);
            targetRings(P, 0.78f, 0.80f, 0.13f);
            return true;
        case Icon::CamStereo:
            movieCamera(P, 0.0f, -0.08f, 0.86f);
            P.text(0.52f, 0.80f, "3D", k.white, 0.46f);
            return true;
        case Icon::CamMotion:
            movieCamera(P, 0.02f, -0.10f, 0.86f);
            P.line(0.34f, 0.60f, 0.34f, 0.90f, k.gray, 2.4f);
            P.line(0.34f, 0.90f, 0.50f, 0.90f, k.gray, 2.4f);
            return true;
        case Icon::CamMorph: {
            const Pen ghost{P.dl, P.o, P.s, P.alpha * 0.45f};
            movieCamera(ghost, 0.12f, 0.12f, 0.80f);
            movieCamera(P, -0.04f, -0.06f, 0.80f);
            return true;
        }
        case Icon::CamCrane:
            P.fill(0.10f, 0.82f, 0.42f, 0.90f, k.gray, 0.02f);
            P.line(0.26f, 0.82f, 0.26f, 0.56f, k.gray, 2.2f);
            P.line(0.08f, 0.66f, 0.62f, 0.44f, k.gray, 2.2f);
            P.disc(0.08f, 0.66f, 0.06f, k.gray);
            movieCamera(P, 0.50f, 0.08f, 0.48f);
            return true;
        case Icon::LgtTarget:
            bulb(P, 0.42f, 0.38f, 0.26f);
            targetRings(P, 0.78f, 0.80f, 0.13f);
            return true;
        case Icon::LgtIes:
            P.tri(0.24f, 0.50f, 0.50f, 0.42f, 0.44f, 0.72f, P.c(255, 240, 180, 170));
            P.quad(0.14f, 0.20f, 0.40f, 0.12f, 0.50f, 0.42f, 0.24f, 0.50f, k.camBody);
            P.quadOutline(0.14f, 0.20f, 0.40f, 0.12f, 0.50f, 0.42f, 0.24f, 0.50f, k.gray, 1.0f);
            cursor(P, 0.58f, 0.52f);
            return true;
        case Icon::LgtSun:
            sun(P, 0.5f, 0.5f, 0.20f, k.yellow);
            P.disc(0.5f, 0.5f, 0.12f, P.c(255, 248, 210));
            return true;
        case Icon::LgtSpot:
            P.tri(0.40f, 0.30f, 0.12f, 0.92f, 0.70f, 0.92f, P.c(255, 240, 180, 120));
            P.quad(0.28f, 0.10f, 0.58f, 0.12f, 0.56f, 0.38f, 0.30f, 0.36f, k.camBody);
            P.quadOutline(0.28f, 0.10f, 0.58f, 0.12f, 0.56f, 0.38f, 0.30f, 0.36f, k.gray, 1.0f);
            P.ellipseFill(0.43f, 0.37f, 0.13f, 0.04f, k.lightWarm);
            return true;
        case Icon::LgtArea:
            P.fill(0.12f, 0.24f, 0.88f, 0.76f, k.white, 0.06f);
            P.rect(0.12f, 0.24f, 0.88f, 0.76f, k.gray, 1.0f);
            P.fill(0.18f, 0.30f, 0.50f, 0.42f, P.c(255, 255, 255), 0.03f);
            return true;
        case Icon::LgtInfinite:
            P.fill(0.10f, 0.10f, 0.90f, 0.90f, k.dark, 0.04f);
            P.tri(0.86f, 0.14f, 0.14f, 0.86f, 0.86f, 0.86f, k.white);
            P.line(0.86f, 0.14f, 0.14f, 0.86f, k.gray, 1.2f);
            return true;
        case Icon::LgtPbr:
            P.fill(0.12f, 0.14f, 0.88f, 0.86f, k.dark, 0.04f);
            for (int i = 0; i < 5; ++i) {
                const float y = 0.22f + i * 0.13f;
                P.fill(0.20f, y, 0.80f, y + 0.07f, k.white);
            }
            P.rect(0.12f, 0.14f, 0.88f, 0.86f, k.gray, 1.0f);
            return true;
        case Icon::LgtTool:
            bulb(P, 0.40f, 0.36f, 0.25f);
            cursor(P, 0.60f, 0.54f);
            return true;
        default:
            return false;
    }
}

} // namespace iron3d::icons::detail
