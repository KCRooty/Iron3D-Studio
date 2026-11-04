#include "C4DIcons.h"
#include "IconPen.h"

#include <imgui_internal.h> // ImTextCharFromUtf8 (texto vertical letra a letra)

namespace iron3d::icons {

using namespace detail;

const char* name(Icon icon) {
    static const char* const kNames[] = {
#define IRON3D_ICON_NAME(n) #n,
        IRON3D_ICON_LIST(IRON3D_ICON_NAME)
#undef IRON3D_ICON_NAME
    };
    const int i = (int)icon;
    return i >= 0 && i < (int)Icon::Count_ ? kNames[i] : "?";
}

void draw(ImDrawList* dl, Icon icon, ImVec2 min, float size, bool enabled) {
    const Pen P{dl, min, size, enabled ? 1.0f : 0.35f};
    const ImU32 gray = P.c(206, 206, 206);
    const ImU32 dark = P.c(40, 40, 40);
    const ImU32 blueTop = P.c(132, 186, 240), blueL = P.c(66, 130, 205), blueR = P.c(44, 96, 160);
    const ImU32 blueEdge = P.c(190, 220, 250);
    const ImU32 red = P.c(225, 60, 55), green = P.c(80, 190, 70), blue = P.c(70, 110, 230);
    const ImU32 orange = P.c(232, 150, 50);
    const ImU32 greenGen = P.c(100, 182, 76), greenGenDark = P.c(58, 126, 46);
    const ImU32 violet = P.c(150, 105, 220);

    if (drawSplineIcon(P, icon) || drawGeneratorIcon(P, icon) || drawDeformerIcon(P, icon) ||
        drawSceneIcon(P, icon) || drawMenuIcon(P, icon))
        return;

    switch (icon) {
        case Icon::Undo:
            P.arc(0.55f, 0.58f, 0.27f, 0.25f * kPi, -1.0f * kPi, gray, 2.0f);
            P.head(0.28f, 0.70f, 0.0f, 1.0f, 0.2f, gray);
            break;
        case Icon::Redo:
            P.arc(0.45f, 0.58f, 0.27f, 0.75f * kPi, 2.0f * kPi, gray, 2.0f);
            P.head(0.72f, 0.70f, 0.0f, 1.0f, 0.2f, gray);
            break;
        case Icon::LiveSelect: {
            const ImVec2 pts[] = {P.p(0.22f, 0.15f), P.p(0.22f, 0.72f), P.p(0.36f, 0.58f), P.p(0.47f, 0.82f),
                                  P.p(0.56f, 0.78f), P.p(0.45f, 0.54f), P.p(0.63f, 0.53f)};
            dl->AddConvexPolyFilled(pts, 7, P.c(235, 235, 235));
            dl->AddPolyline(pts, 7, dark, ImDrawFlags_Closed, P.w(1.0f));
            for (int i = 0; i < 10; i += 2)
                dl->PathArcTo(P.p(0.66f, 0.64f), 0.2f * size, i * kPi / 5.0f, (i + 1) * kPi / 5.0f, 4),
                    dl->PathStroke(orange, 0, P.w(1.4f));
            break;
        }
        case Icon::Move:
            P.line(0.5f, 0.14f, 0.5f, 0.86f, gray, 1.6f);
            P.line(0.14f, 0.5f, 0.86f, 0.5f, gray, 1.6f);
            P.head(0.5f, 0.08f, 0, -1, 0.17f, gray);
            P.head(0.5f, 0.92f, 0, 1, 0.17f, gray);
            P.head(0.08f, 0.5f, -1, 0, 0.17f, gray);
            P.head(0.92f, 0.5f, 1, 0, 0.17f, gray);
            break;
        case Icon::Scale:
            P.rect(0.14f, 0.52f, 0.48f, 0.86f, gray, 1.4f);
            P.rect(0.14f, 0.14f, 0.86f, 0.86f, P.c(206, 206, 206, 110), 1.0f);
            P.line(0.36f, 0.64f, 0.76f, 0.24f, gray, 1.8f);
            P.head(0.82f, 0.18f, 1, -1, 0.2f, gray);
            break;
        case Icon::Rotate:
            P.arc(0.5f, 0.52f, 0.32f, -0.35f * kPi, 1.35f * kPi, gray, 2.0f);
            P.head(0.78f, 0.30f, 0.6f, 1.0f, 0.2f, gray);
            P.disc(0.5f, 0.52f, 0.06f, gray);
            break;
        case Icon::AxisX:
        case Icon::AxisY:
        case Icon::AxisZ: {
            const ImU32 col = icon == Icon::AxisX ? red : (icon == Icon::AxisY ? green : blue);
            P.disc(0.5f, 0.5f, 0.38f, P.c(48, 48, 52));
            P.circle(0.5f, 0.5f, 0.38f, col, 1.6f);
            P.text(0.5f, 0.5f, icon == Icon::AxisX ? "X" : (icon == Icon::AxisY ? "Y" : "Z"), col, 0.55f);
            break;
        }
        case Icon::CoordSystem:
            P.circle(0.5f, 0.5f, 0.34f, gray, 1.4f);
            dl->AddEllipse(P.p(0.5f, 0.5f), ImVec2(0.14f * size, 0.34f * size), gray, 0.0f, 0, P.w(1.1f));
            P.line(0.16f, 0.5f, 0.84f, 0.5f, gray, 1.1f);
            P.line(0.5f, 0.5f, 0.85f, 0.5f, red, 1.8f);
            P.line(0.5f, 0.5f, 0.5f, 0.15f, green, 1.8f);
            break;
        case Icon::RenderView:
            clapper(P);
            break;
        case Icon::RenderPicture:
            clapper(P);
            P.fill(0.50f, 0.56f, 0.92f, 0.92f, P.c(120, 170, 220));
            P.tri(0.53f, 0.89f, 0.68f, 0.68f, 0.82f, 0.89f, P.c(70, 120, 60));
            P.rect(0.50f, 0.56f, 0.92f, 0.92f, gray, 1.0f);
            break;
        case Icon::RenderSettings:
            clapper(P);
            gear(P, 0.72f, 0.73f, 0.2f, P.c(225, 225, 225));
            break;
        case Icon::Cube:
            isoCube(P, 0.5f, 0.5f, 0.36f, blueTop, blueL, blueR, blueEdge);
            break;
        case Icon::Spline:
            P.quad(0.70f, 0.12f, 0.88f, 0.30f, 0.38f, 0.80f, 0.20f, 0.62f, P.c(225, 225, 225));
            P.tri(0.20f, 0.62f, 0.38f, 0.80f, 0.12f, 0.88f, orange);
            P.quad(0.70f, 0.12f, 0.88f, 0.30f, 0.80f, 0.38f, 0.62f, 0.20f, orange);
            P.quadOutline(0.70f, 0.12f, 0.88f, 0.30f, 0.38f, 0.80f, 0.20f, 0.62f, dark, 0.8f);
            break;
        case Icon::Nurbs:
            P.fill(0.18f, 0.18f, 0.82f, 0.82f, greenGen, 0.2f);
            P.line(0.18f, 0.5f, 0.82f, 0.5f, greenGenDark, 1.0f);
            P.line(0.5f, 0.18f, 0.5f, 0.82f, greenGenDark, 1.0f);
            dl->AddEllipse(P.p(0.5f, 0.5f), ImVec2(0.32f * size, 0.14f * size), greenGenDark, 0.0f, 0, P.w(1.0f));
            P.disc(0.36f, 0.34f, 0.07f, P.c(200, 240, 180, 160));
            break;
        case Icon::Cloner:
            for (int y = 0; y < 3; ++y)
                for (int x = 0; x < 3; ++x)
                    P.disc(0.24f + x * 0.26f, 0.24f + y * 0.26f, 0.095f, P.c(70, 200, 150));
            break;
        case Icon::Deformer: {
            ImVec2 pts[12];
            for (int i = 0; i < 12; ++i) {
                const float t = i / 11.0f;
                pts[i] = P.p(0.25f + std::sin(t * kPi) * 0.38f, 0.12f + t * 0.76f);
            }
            dl->AddPolyline(pts, 12, violet, 0, P.w(5.0f));
            dl->AddPolyline(pts, 12, P.c(200, 170, 250), 0, P.w(1.4f));
            break;
        }
        case Icon::Floor:
            P.quad(0.30f, 0.38f, 0.92f, 0.38f, 0.70f, 0.78f, 0.08f, 0.78f, P.c(150, 150, 160));
            P.line(0.19f, 0.58f, 0.81f, 0.58f, P.c(110, 110, 118), 1.0f);
            P.line(0.61f, 0.38f, 0.39f, 0.78f, P.c(110, 110, 118), 1.0f);
            P.disc(0.72f, 0.20f, 0.1f, P.c(250, 220, 120));
            break;
        case Icon::Camera:
            P.fill(0.10f, 0.32f, 0.66f, 0.76f, P.c(70, 70, 76), 0.05f);
            P.rect(0.10f, 0.32f, 0.66f, 0.76f, gray, 1.0f);
            P.tri(0.66f, 0.54f, 0.92f, 0.34f, 0.92f, 0.74f, P.c(70, 70, 76));
            P.disc(0.27f, 0.22f, 0.11f, P.c(120, 160, 220));
            P.disc(0.50f, 0.22f, 0.11f, P.c(120, 160, 220));
            break;
        case Icon::Light:
            P.disc(0.5f, 0.42f, 0.30f, P.c(245, 245, 235));
            P.circle(0.5f, 0.42f, 0.30f, P.c(160, 160, 150), 1.0f);
            P.fill(0.38f, 0.70f, 0.62f, 0.88f, P.c(150, 150, 150), 0.03f);
            P.line(0.38f, 0.76f, 0.62f, 0.76f, dark, 1.0f);
            P.disc(0.5f, 0.42f, 0.12f, P.c(255, 230, 140));
            break;
        case Icon::MakeEditable:
            wireCube(P, gray);
            P.disc(0.18f, 0.35f, 0.06f, orange);
            P.disc(0.62f, 0.82f, 0.06f, orange);
            P.disc(0.82f, 0.18f, 0.06f, orange);
            break;
        case Icon::ModelMode:
            isoCube(P, 0.48f, 0.54f, 0.32f, blueTop, blueL, blueR, blueEdge);
            P.line(0.50f, 0.50f, 0.88f, 0.50f, red, 1.6f);
            P.line(0.50f, 0.50f, 0.50f, 0.10f, green, 1.6f);
            break;
        case Icon::TextureMode:
            for (int y = 0; y < 4; ++y)
                for (int x = 0; x < 4; ++x)
                    P.fill(0.14f + x * 0.18f, 0.14f + y * 0.18f, 0.32f + x * 0.18f, 0.32f + y * 0.18f,
                           ((x + y) & 1) ? P.c(60, 60, 60) : P.c(225, 225, 225));
            break;
        case Icon::AxisMode:
            P.line(0.25f, 0.78f, 0.85f, 0.78f, red, 2.0f);
            P.line(0.25f, 0.78f, 0.25f, 0.15f, green, 2.0f);
            P.line(0.25f, 0.78f, 0.55f, 0.48f, blue, 2.0f);
            break;
        case Icon::Points:
            wireCube(P, P.c(150, 150, 150));
            for (float x : {0.18f, 0.62f})
                for (float y : {0.35f, 0.82f}) P.disc(x, y, 0.06f, orange);
            for (float x : {0.38f, 0.82f})
                for (float y : {0.18f, 0.65f}) P.disc(x, y, 0.06f, orange);
            break;
        case Icon::Edges:
            wireCube(P, P.c(150, 150, 150));
            P.line(0.18f, 0.35f, 0.62f, 0.35f, orange, 2.2f);
            P.line(0.62f, 0.35f, 0.82f, 0.18f, orange, 2.2f);
            P.line(0.62f, 0.35f, 0.62f, 0.82f, orange, 2.2f);
            break;
        case Icon::Polygons:
            wireCube(P, P.c(150, 150, 150));
            P.quad(0.18f, 0.35f, 0.62f, 0.35f, 0.62f, 0.82f, 0.18f, 0.82f, P.c(232, 150, 50, 200));
            break;
        case Icon::Tweak: {
            const ImVec2 pts[] = {P.p(0.30f, 0.12f), P.p(0.30f, 0.66f), P.p(0.43f, 0.53f), P.p(0.53f, 0.76f),
                                  P.p(0.61f, 0.72f), P.p(0.51f, 0.50f), P.p(0.68f, 0.49f)};
            dl->AddConvexPolyFilled(pts, 7, gray);
            P.disc(0.74f, 0.78f, 0.1f, orange);
            break;
        }
        case Icon::Solo:
            P.circle(0.5f, 0.5f, 0.34f, gray, 1.4f);
            P.disc(0.5f, 0.5f, 0.16f, P.c(255, 220, 120));
            break;
        case Icon::Snap:
            P.arc(0.5f, 0.45f, 0.26f, kPi, 2.0f * kPi, red, 5.0f);
            P.line(0.24f, 0.45f, 0.24f, 0.78f, red, 5.0f);
            P.line(0.76f, 0.45f, 0.76f, 0.78f, red, 5.0f);
            P.fill(0.17f, 0.72f, 0.31f, 0.86f, P.c(220, 220, 220));
            P.fill(0.69f, 0.72f, 0.83f, 0.86f, P.c(220, 220, 220));
            break;
        case Icon::Workplane:
            for (int i = 0; i < 4; ++i) {
                const float t = i / 3.0f;
                P.line(0.30f + t * 0.62f - 0.22f * 0, 0.36f, 0.08f + t * 0.62f, 0.80f, P.c(200, 200, 120), 1.0f);
                P.line(0.30f - t * 0.22f, 0.36f + t * 0.44f, 0.92f - t * 0.22f, 0.36f + t * 0.44f,
                       P.c(200, 200, 120), 1.0f);
            }
            break;
        case Icon::Locked:
        case Icon::Lock:
            P.arc(0.5f, 0.40f, 0.18f, kPi, 2.0f * kPi, gray, 2.0f);
            P.line(0.32f, 0.40f, 0.32f, 0.50f, gray, 2.0f);
            P.line(0.68f, 0.40f, 0.68f, 0.50f, gray, 2.0f);
            P.fill(0.24f, 0.48f, 0.76f, 0.86f, gray, 0.04f);
            P.disc(0.5f, 0.64f, 0.06f, dark);
            break;
        case Icon::Null:
            P.line(0.5f, 0.12f, 0.5f, 0.88f, P.c(190, 190, 190), 1.4f);
            P.line(0.12f, 0.5f, 0.88f, 0.5f, P.c(190, 190, 190), 1.4f);
            P.line(0.24f, 0.76f, 0.76f, 0.24f, P.c(190, 190, 190), 1.4f);
            P.disc(0.5f, 0.5f, 0.1f, P.c(160, 160, 160));
            break;
        case Icon::Sphere:
            materialBall(dl, P.p(0.5f, 0.5f), 0.38f * size,
                         ImVec4(0.30f, 0.55f, 0.88f, P.alpha), 0.6f, 40.0f);
            break;
        case Icon::Plane:
            P.quad(0.32f, 0.30f, 0.94f, 0.30f, 0.68f, 0.72f, 0.06f, 0.72f, blueL);
            P.quadOutline(0.32f, 0.30f, 0.94f, 0.30f, 0.68f, 0.72f, 0.06f, 0.72f, blueEdge, 0.9f);
            break;
        case Icon::Cone:
            P.tri(0.5f, 0.12f, 0.84f, 0.76f, 0.16f, 0.76f, blueL);
            dl->AddEllipseFilled(P.p(0.5f, 0.76f), ImVec2(0.34f * size, 0.1f * size), blueR);
            break;
        case Icon::Cylinder:
            P.fill(0.2f, 0.26f, 0.8f, 0.76f, blueL);
            dl->AddEllipseFilled(P.p(0.5f, 0.76f), ImVec2(0.3f * size, 0.1f * size), blueR);
            dl->AddEllipseFilled(P.p(0.5f, 0.26f), ImVec2(0.3f * size, 0.1f * size), blueTop);
            break;
        case Icon::Disc:
            dl->AddEllipseFilled(P.p(0.5f, 0.55f), ImVec2(0.4f * size, 0.18f * size), blueL);
            dl->AddEllipseFilled(P.p(0.5f, 0.55f), ImVec2(0.14f * size, 0.06f * size), P.c(60, 60, 60));
            break;
        case Icon::Torus:
            dl->AddEllipse(P.p(0.5f, 0.55f), ImVec2(0.3f * size, 0.16f * size), blueL, 0.0f, 0, P.w(5.0f));
            break;
        case Icon::Capsule:
            P.fill(0.30f, 0.12f, 0.70f, 0.88f, blueL, 0.2f);
            P.fill(0.36f, 0.18f, 0.48f, 0.82f, blueTop, 0.06f);
            break;
        case Icon::Tube:
            P.fill(0.2f, 0.26f, 0.8f, 0.76f, blueL);
            dl->AddEllipseFilled(P.p(0.5f, 0.26f), ImVec2(0.3f * size, 0.1f * size), blueTop);
            dl->AddEllipseFilled(P.p(0.5f, 0.26f), ImVec2(0.15f * size, 0.05f * size), P.c(40, 40, 40));
            break;
        case Icon::Pyramid:
            P.tri(0.5f, 0.12f, 0.86f, 0.70f, 0.5f, 0.84f, blueR);
            P.tri(0.5f, 0.12f, 0.5f, 0.84f, 0.14f, 0.70f, blueL);
            break;
        case Icon::Platonic: {
            ImVec2 pts[6];
            for (int i = 0; i < 6; ++i)
                pts[i] = P.p(0.5f + std::cos(i * kPi / 3 + kPi / 6) * 0.38f, 0.5f + std::sin(i * kPi / 3 + kPi / 6) * 0.38f);
            dl->AddConvexPolyFilled(pts, 6, blueL);
            P.line(0.5f, 0.12f, 0.5f, 0.88f, blueEdge, 0.8f);
            P.line(0.17f, 0.31f, 0.83f, 0.69f, blueEdge, 0.8f);
            break;
        }
        case Icon::Figure:
            P.disc(0.5f, 0.2f, 0.1f, blueL);
            P.line(0.5f, 0.3f, 0.5f, 0.62f, blueL, 3.0f);
            P.line(0.24f, 0.42f, 0.76f, 0.42f, blueL, 3.0f);
            P.line(0.5f, 0.62f, 0.34f, 0.88f, blueL, 3.0f);
            P.line(0.5f, 0.62f, 0.66f, 0.88f, blueL, 3.0f);
            break;
        case Icon::Landscape:
            P.tri(0.08f, 0.82f, 0.38f, 0.30f, 0.62f, 0.82f, blueL);
            P.tri(0.38f, 0.82f, 0.66f, 0.18f, 0.94f, 0.82f, blueR);
            break;
        case Icon::Relief: {
            ImVec2 pts[10];
            for (int i = 0; i < 10; ++i) pts[i] = P.p(0.08f + i * 0.093f, 0.55f + std::sin(i * 1.3f) * 0.18f);
            dl->AddPolyline(pts, 10, blueL, 0, P.w(3.0f));
            break;
        }
        case Icon::Polygon:
            P.tri(0.15f, 0.80f, 0.85f, 0.80f, 0.50f, 0.20f, blueL);
            break;
        case Icon::OilTank:
            P.fill(0.16f, 0.28f, 0.84f, 0.74f, blueL, 0.18f);
            P.fill(0.22f, 0.34f, 0.40f, 0.68f, blueTop, 0.06f);
            break;
        case Icon::EmptyPolygon:
            for (int i = 0; i < 3; ++i) {
                const float xs[] = {0.15f, 0.85f, 0.50f, 0.15f};
                const float ys[] = {0.80f, 0.80f, 0.20f, 0.80f};
                P.line(xs[i], ys[i], xs[i + 1], ys[i + 1], blueL, 1.2f);
            }
            break;
        case Icon::Guide:
            for (int i = 0; i < 5; ++i) P.line(0.1f + i * 0.17f, 0.5f, 0.18f + i * 0.17f, 0.5f, P.c(240, 200, 80), 2.0f);
            break;
        case Icon::NavPan:
            P.line(0.5f, 0.18f, 0.5f, 0.82f, gray, 1.4f);
            P.line(0.18f, 0.5f, 0.82f, 0.5f, gray, 1.4f);
            P.head(0.5f, 0.1f, 0, -1, 0.16f, gray);
            P.head(0.5f, 0.9f, 0, 1, 0.16f, gray);
            P.head(0.1f, 0.5f, -1, 0, 0.16f, gray);
            P.head(0.9f, 0.5f, 1, 0, 0.16f, gray);
            break;
        case Icon::NavZoom:
            P.tri(0.5f, 0.10f, 0.78f, 0.42f, 0.22f, 0.42f, gray);
            P.tri(0.5f, 0.90f, 0.22f, 0.58f, 0.78f, 0.58f, gray);
            break;
        case Icon::NavRotate:
            P.arc(0.5f, 0.5f, 0.32f, -0.9f * kPi, 0.6f * kPi, gray, 1.8f);
            P.head(0.24f, 0.32f, -0.4f, -1.0f, 0.18f, gray);
            break;
        case Icon::NavLayout:
            P.rect(0.14f, 0.14f, 0.46f, 0.46f, gray, 1.2f);
            P.rect(0.54f, 0.14f, 0.86f, 0.46f, gray, 1.2f);
            P.rect(0.14f, 0.54f, 0.46f, 0.86f, gray, 1.2f);
            P.rect(0.54f, 0.54f, 0.86f, 0.86f, gray, 1.2f);
            break;
        case Icon::PhongTag:
            materialBall(dl, P.p(0.5f, 0.5f), 0.36f * size, ImVec4(0.72f, 0.72f, 0.72f, P.alpha), 0.9f, 20.0f);
            P.arc(0.5f, 0.5f, 0.36f, -0.15f * kPi, 0.45f * kPi, orange, 1.6f);
            break;
        case Icon::Play:
            P.tri(0.30f, 0.18f, 0.80f, 0.5f, 0.30f, 0.82f, gray);
            break;
        case Icon::Pause:
            P.fill(0.28f, 0.2f, 0.42f, 0.8f, gray);
            P.fill(0.58f, 0.2f, 0.72f, 0.8f, gray);
            break;
        case Icon::PlayBack:
            P.tri(0.70f, 0.18f, 0.20f, 0.5f, 0.70f, 0.82f, gray);
            break;
        case Icon::StepFwd:
            P.tri(0.24f, 0.22f, 0.64f, 0.5f, 0.24f, 0.78f, gray);
            P.fill(0.66f, 0.22f, 0.76f, 0.78f, gray);
            break;
        case Icon::StepBack:
            P.tri(0.76f, 0.22f, 0.36f, 0.5f, 0.76f, 0.78f, gray);
            P.fill(0.24f, 0.22f, 0.34f, 0.78f, gray);
            break;
        case Icon::ToStart:
            P.fill(0.18f, 0.22f, 0.28f, 0.78f, gray);
            P.tri(0.56f, 0.22f, 0.28f, 0.5f, 0.56f, 0.78f, gray);
            P.tri(0.84f, 0.22f, 0.56f, 0.5f, 0.84f, 0.78f, gray);
            break;
        case Icon::ToEnd:
            P.fill(0.72f, 0.22f, 0.82f, 0.78f, gray);
            P.tri(0.44f, 0.22f, 0.72f, 0.5f, 0.44f, 0.78f, gray);
            P.tri(0.16f, 0.22f, 0.44f, 0.5f, 0.16f, 0.78f, gray);
            break;
        case Icon::NextKey:
            P.tri(0.22f, 0.22f, 0.58f, 0.5f, 0.22f, 0.78f, gray);
            P.quad(0.70f, 0.30f, 0.86f, 0.5f, 0.70f, 0.70f, 0.54f, 0.5f, gray);
            break;
        case Icon::PrevKey:
            P.tri(0.78f, 0.22f, 0.42f, 0.5f, 0.78f, 0.78f, gray);
            P.quad(0.30f, 0.30f, 0.46f, 0.5f, 0.30f, 0.70f, 0.14f, 0.5f, gray);
            break;
        case Icon::Record:
            P.disc(0.5f, 0.5f, 0.32f, red);
            P.disc(0.42f, 0.42f, 0.1f, P.c(255, 160, 150));
            break;
        case Icon::AutoKey:
            P.disc(0.5f, 0.5f, 0.32f, P.c(150, 40, 40));
            P.circle(0.5f, 0.5f, 0.32f, red, 1.4f);
            P.text(0.5f, 0.52f, "A", P.c(255, 220, 220), 0.5f);
            break;
        case Icon::KeySel:
            P.quad(0.5f, 0.16f, 0.84f, 0.5f, 0.5f, 0.84f, 0.16f, 0.5f, P.c(150, 40, 40));
            P.quadOutline(0.5f, 0.16f, 0.84f, 0.5f, 0.5f, 0.84f, 0.16f, 0.5f, red, 1.2f);
            break;
        case Icon::KeyPos:
            P.line(0.5f, 0.2f, 0.5f, 0.8f, P.c(110, 160, 230), 1.6f);
            P.line(0.2f, 0.5f, 0.8f, 0.5f, P.c(110, 160, 230), 1.6f);
            P.head(0.5f, 0.12f, 0, -1, 0.14f, P.c(110, 160, 230));
            P.head(0.88f, 0.5f, 1, 0, 0.14f, P.c(110, 160, 230));
            break;
        case Icon::KeyScale:
            P.rect(0.2f, 0.2f, 0.8f, 0.8f, P.c(110, 160, 230), 1.4f);
            P.fill(0.2f, 0.56f, 0.44f, 0.8f, P.c(110, 160, 230));
            break;
        case Icon::KeyRot:
            P.arc(0.5f, 0.5f, 0.3f, -0.3f * kPi, 1.3f * kPi, P.c(110, 160, 230), 1.8f);
            break;
        case Icon::KeyParam:
            P.text(0.5f, 0.5f, "P", P.c(110, 160, 230), 0.7f);
            break;
        case Icon::KeyPLA:
            P.text(0.5f, 0.5f, "PLA", P.c(110, 160, 230), 0.42f);
            break;
        case Icon::Check:
            P.line(0.18f, 0.52f, 0.42f, 0.76f, P.c(110, 200, 90), 2.6f);
            P.line(0.42f, 0.76f, 0.84f, 0.24f, P.c(110, 200, 90), 2.6f);
            break;
        case Icon::Search:
            P.circle(0.42f, 0.42f, 0.24f, gray, 1.8f);
            P.line(0.60f, 0.60f, 0.86f, 0.86f, gray, 2.6f);
            break;
        case Icon::Back:
            P.tri(0.68f, 0.2f, 0.28f, 0.5f, 0.68f, 0.8f, gray);
            break;
        case Icon::Forward:
            P.tri(0.32f, 0.2f, 0.72f, 0.5f, 0.32f, 0.8f, gray);
            break;
        case Icon::Up:
            P.tri(0.2f, 0.68f, 0.5f, 0.28f, 0.8f, 0.68f, gray);
            break;
        case Icon::NewPanel:
            P.rect(0.16f, 0.24f, 0.70f, 0.80f, gray, 1.2f);
            P.line(0.74f, 0.12f, 0.74f, 0.40f, gray, 1.6f);
            P.line(0.60f, 0.26f, 0.88f, 0.26f, gray, 1.6f);
            break;
        case Icon::Gear:
            gear(P, 0.5f, 0.5f, 0.36f, gray);
            break;
        case Icon::Filter:
            P.tri(0.14f, 0.2f, 0.86f, 0.2f, 0.5f, 0.56f, gray);
            P.fill(0.44f, 0.5f, 0.56f, 0.86f, gray);
            break;
        default:
            break;
    }
}

void materialBall(ImDrawList* dl, ImVec2 center, float radius, const ImVec4& color, float specular,
                  float shininess) {
    // Degradado radial: anillos concentricos desplazados hacia la luz (arriba
    // a la izquierda), de borde oscuro a centro iluminado -- lectura Lambert
    // aproximada, sin shader.
    const int steps = std::clamp((int)(radius * 0.9f), 8, 40);
    const ImVec2 lightDir(-0.32f, -0.38f);
    for (int i = 0; i < steps; ++i) {
        const float t = (float)i / (float)(steps - 1);
        const float r = radius * (1.0f - t * 0.92f);
        const ImVec2 c(center.x + lightDir.x * radius * t * 0.75f, center.y + lightDir.y * radius * t * 0.75f);
        const float shade = 0.18f + 0.95f * std::pow(t, 0.7f);
        const ImVec4 col(std::min(1.0f, color.x * shade + 0.02f), std::min(1.0f, color.y * shade + 0.02f),
                         std::min(1.0f, color.z * shade + 0.02f), color.w);
        dl->AddCircleFilled(c, r, ImGui::ColorConvertFloat4ToU32(col), 32);
    }
    // Brillo especular: mas pequeno y concentrado cuanto mas alto el exponente.
    const float spot = radius * std::clamp(0.42f - shininess / 400.0f, 0.08f, 0.42f);
    const ImVec2 hc(center.x + lightDir.x * radius * 0.82f, center.y + lightDir.y * radius * 0.82f);
    for (int i = 0; i < 6; ++i) {
        const float t = (float)i / 5.0f;
        const float a = std::clamp(specular, 0.0f, 1.0f) * 0.22f * color.w;
        dl->AddCircleFilled(hc, spot * (1.0f - t * 0.8f), ImGui::ColorConvertFloat4ToU32(ImVec4(1, 1, 1, a)), 24);
    }
}

void textVertical(ImDrawList* dl, ImVec2 topRight, ImU32 col, const char* text) {
    // Letra a letra: AddText descarta los glifos fuera del recorte de la
    // ventana ANTES del giro, y en una franja de 20 px de ancho un texto
    // entero se perderia salvo la primera letra. Cada letra se dibuja en su
    // propio pivote (dentro de la franja) y se gira 90 grados horario
    // alrededor de el: +x pasa a +y y +y a -x.
    ImFont* font = ImGui::GetFont();
    const float scale = ImGui::GetFontSize() / font->FontSize;
    float advance = 0.0f;
    const char* s = text;
    while (*s) {
        unsigned int c = 0;
        const char* next = s + ImTextCharFromUtf8(&c, s, nullptr);
        const ImVec2 pivot(topRight.x, topRight.y + advance);
        const int start = dl->VtxBuffer.Size;
        dl->AddText(pivot, col, s, next);
        for (int i = start; i < dl->VtxBuffer.Size; ++i) {
            ImDrawVert& v = dl->VtxBuffer[i];
            const float dx = v.pos.x - pivot.x;
            const float dy = v.pos.y - pivot.y;
            v.pos.x = pivot.x - dy;
            v.pos.y = pivot.y + dx;
        }
        advance += font->GetCharAdvance((ImWchar)c) * scale;
        s = next;
    }
}

float textVerticalLength(const char* text) { return ImGui::CalcTextSize(text).x; }

} // namespace iron3d::icons
