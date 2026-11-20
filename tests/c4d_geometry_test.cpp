// Pruebas de la geometria compatible con C4D (importers/c4d_compat). Sin GPU
// ni Cineware: se ejecutan en el CI de Windows y Linux.

#include "core/SceneNode.h"
#include "core/MeshEdit.h"
#include "core/Boolean.h"
#include "core/Clone.h"
#include "core/Deform.h"
#include "core/MeshTopology.h"
#include "core/SceneSerializer.h"
#include "core/ThumbnailRenderer.h"
#include "core/TransformGizmo.h"
#include "importers/c4d_compat/C4DGeometry.h"
#include "io_scene_fixture.h"
#include "core/Paths.h"

#include <stb_image_write.h>

#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <set>

using namespace iron3d;

namespace {

int g_failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        ++g_failures;
        std::printf("FALLO: %s\n", what);
    }
}

// Todas las caras de la malla de render miran hacia fuera segun `outward`
// (direccion "hacia fuera" en el centro de cada triangulo).
bool facesPointOut(const MeshData& m, const std::function<glm::vec3(const glm::vec3&)>& outward) {
    for (size_t i = 0; i + 2 < m.indices.size(); i += 3) {
        const glm::vec3 a = m.vertices[m.indices[i]].position, b = m.vertices[m.indices[i + 1]].position,
                        c = m.vertices[m.indices[i + 2]].position;
        const glm::vec3 n = glm::cross(b - a, c - a);
        if (glm::length(n) < 1e-12f) continue;
        if (glm::dot(n, outward((a + b + c) / 3.0f)) <= 0.0f) return false;
        // La normal de los vertices debe ir hacia el mismo lado que la cara.
        if (glm::dot(m.vertices[m.indices[i]].normal, n) <= 0.0f) return false;
    }
    return !m.indices.empty();
}

glm::vec3 fromCenter(const glm::vec3& p) { return p; }

double kPi80() { return 80.0 * 3.14159265358979323846 / 180.0; }

bool nearly(const glm::vec3& a, const glm::vec3& b, float eps = 1e-4f) { return glm::length(a - b) < eps; }

} // namespace

int main() {
    // --- Primitivas: orientacion de caras tras el espejo de C4D -> Iron3D ----
    {
        const MeshData cube = c4d::toIronMesh(c4d::makeCube({200.0, 200.0, 200.0}), -1.0);
        check(cube.indices.size() == 36, "cubo: 12 triangulos");
        check(facesPointOut(cube, fromCenter), "cubo: caras hacia fuera");
        check(nearly(cube.boundsMin, glm::vec3(-0.5f)) && nearly(cube.boundsMax, glm::vec3(0.5f)),
              "cubo de 200 cm = 1 unidad");
    }
    {
        const MeshData s = c4d::toIronMesh(c4d::makeSphere(100.0, 24), 80.0 * 3.14159265 / 180.0);
        check(facesPointOut(s, fromCenter), "esfera: caras hacia fuera");
        check(nearly(s.boundsMax, glm::vec3(0.5f), 1e-3f), "esfera de radio 100 cm");
    }
    for (int axis = 0; axis < 6; ++axis) {
        const MeshData cyl = c4d::toIronMesh(c4d::makeCylinder(50.0, 200.0, 16, true, (c4d::Axis)axis), -1.0);
        check(facesPointOut(cyl, fromCenter), "cilindro: caras hacia fuera (todos los ejes)");
        const MeshData plane = c4d::toIronMesh(c4d::makePlane(400.0, 400.0, (c4d::Axis)axis), -1.0);
        check(plane.indices.size() == 6, "plano: 2 triangulos");
    }
    {
        // Cono: el centro del volumen no es el origen; basta con que la caja
        // sea la esperada y la base mire hacia abajo.
        const MeshData cone = c4d::toIronMesh(c4d::makeCone(0.0, 100.0, 200.0, 16, true, c4d::Axis::PosY), -1.0);
        check(nearly(cone.boundsMin, glm::vec3(-0.5f, -0.5f, -0.5f), 1e-3f), "cono: caja");
        check(facesPointOut(cone, [](const glm::vec3& p) { return p - glm::vec3(0.0f, -0.25f, 0.0f); }),
              "cono: caras hacia fuera");
    }
    {
        const MeshData t = c4d::toIronMesh(c4d::makeTorus(200.0, 50.0, 24, 12, c4d::Axis::PosY), 1.4);
        check(facesPointOut(t, [](const glm::vec3& p) {
                  glm::vec3 ring(p.x, 0.0f, p.z);
                  ring = glm::length(ring) > 1e-6f ? glm::normalize(ring) * 1.0f : ring; // 200 cm = 1 unidad
                  return p - ring;
              }),
              "toro: caras hacia fuera");
    }
    {
        const MeshData py = c4d::toIronMesh(c4d::makePyramid({200.0, 200.0, 200.0}), -1.0);
        check(facesPointOut(py, [](const glm::vec3& p) { return p - glm::vec3(0.0f, -0.25f, 0.0f); }),
              "piramide: caras hacia fuera");
        const MeshData disc = c4d::toIronMesh(c4d::makeDisc(20.0, 100.0, 16, c4d::Axis::PosY), -1.0);
        check(facesPointOut(disc, [](const glm::vec3&) { return glm::vec3(0.0f, 1.0f, 0.0f); }), "disco hacia +Y");
    }
    {
        // Malla escrita "a mano" con el orden de C4D (la piramide de la
        // prueba con Cineware): base (0,1,2,3) y 4 lados hacia la punta.
        c4d::PolySoup s;
        s.points = {{100, 0, 100}, {-100, 0, 100}, {-100, 0, -100}, {100, 0, -100}, {0, 200, 0}};
        s.polys = {{0, 1, 2, 3}, {4, 1, 0, 0}, {4, 2, 1, 1}, {4, 3, 2, 2}, {4, 0, 3, 3}};
        const MeshData m = c4d::toIronMesh(s, -1.0);
        check(facesPointOut(m, [](const glm::vec3& p) { return p - glm::vec3(0.0f, 0.25f, 0.0f); }),
              "malla de C4D: caras hacia fuera");
    }

    // --- Matriz de C4D -> TRS de SceneNode ------------------------------------
    // Invariante: convertir el punto ya transformado en C4D == aplicar el TRS
    // de Iron3D al punto convertido.
    {
        auto c4dApply = [](const glm::dvec3& off, const glm::dvec3& v1, const glm::dvec3& v2, const glm::dvec3& v3,
                           const glm::dvec3& p) { return off + p.x * v1 + p.y * v2 + p.z * v3; };
        struct Case { glm::dvec3 off, v1, v2, v3; const char* name; };
        const double c = std::cos(0.7), s = std::sin(0.7), c2 = std::cos(-1.1), s2 = std::sin(-1.1);
        // Rotacion compuesta (alrededor de Y y luego X) con escala no uniforme.
        const glm::dmat3 ry(glm::dvec3(c, 0, -s), glm::dvec3(0, 1, 0), glm::dvec3(s, 0, c));
        const glm::dmat3 rx(glm::dvec3(1, 0, 0), glm::dvec3(0, c2, s2), glm::dvec3(0, -s2, c2));
        const glm::dmat3 r = rx * ry;
        const Case cases[] = {
            {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}, "identidad"},
            {{100, 50, 200}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}, "traslacion"},
            {{10, -20, 30}, r[0] * 2.0, r[1] * 0.5, r[2] * 1.5, "rotacion + escala"},
            {{0, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, 0, 1}, "espejo en X"},
        };
        for (const Case& k : cases) {
            const c4d::TRS trs = c4d::toIronTRS(k.off, k.v1, k.v2, k.v3);
            SceneNode n("t");
            n.position = trs.position;
            n.rotationDeg = trs.rotationDeg;
            n.scale = trs.scale;
            const glm::mat4 m = n.localMatrix();
            const glm::dvec3 pts[] = {{0, 0, 0}, {100, 0, 0}, {0, 100, 0}, {0, 0, 100}, {30, -70, 45}};
            bool ok = true;
            for (const glm::dvec3& p : pts) {
                const glm::vec3 expect = c4d::toIronPoint(c4dApply(k.off, k.v1, k.v2, k.v3, p));
                const glm::vec3 got = glm::vec3(m * glm::vec4(c4d::toIronPoint(p), 1.0f));
                ok = ok && nearly(expect, got, 1e-3f);
            }
            check(ok, k.name);
        }
    }

    // --- Exportar: TRS de SceneNode -> matriz de C4D (inversa) --------------
    {
        struct Case { glm::vec3 pos, rot, scl; const char* name; };
        const Case cases[] = {
            {{0, 0, 0}, {0, 0, 0}, {1, 1, 1}, "exportar: identidad"},
            {{1.5f, -0.25f, 3.0f}, {30, -45, 60}, {2, 0.5f, 1.25f}, "exportar: rotacion + escala"},
            {{0, 1, 0}, {-120, 10, 170}, {-1, 1, 1}, "exportar: espejo en X"},
        };
        for (const Case& k : cases) {
            SceneNode n("t");
            n.position = k.pos;
            n.rotationDeg = k.rot;
            n.scale = k.scl;
            const glm::mat4 m = n.localMatrix();
            const c4d::Matrix cm = c4d::fromIronTRS(k.pos, k.rot, k.scl);
            const glm::vec3 pts[] = {{0, 0, 0}, {0.5f, 0, 0}, {0, 0.5f, 0}, {0, 0, 0.5f}, {0.15f, -0.35f, 0.2f}};
            bool ok = true;
            for (const glm::vec3& p : pts) {
                const glm::dvec3 q = c4d::fromIronPoint(p);
                const glm::dvec3 c4dPoint = cm.off + q.x * cm.v1 + q.y * cm.v2 + q.z * cm.v3;
                ok = ok && nearly(c4d::toIronPoint(c4dPoint), glm::vec3(m * glm::vec4(p, 1.0f)), 1e-4f);
            }
            // Y de vuelta: toIronTRS(fromIronTRS(x)) da la misma matriz.
            const c4d::TRS back = c4d::toIronTRS(cm.off, cm.v1, cm.v2, cm.v3);
            SceneNode b("b");
            b.position = back.position;
            b.rotationDeg = back.rotationDeg;
            b.scale = back.scale;
            const glm::mat4 mb = b.localMatrix();
            for (int c = 0; c < 4; ++c) ok = ok && nearly(glm::vec3(mb[c]), glm::vec3(m[c]), 1e-4f);
            check(ok, k.name);
        }
    }

    // --- Exportar: malla de render -> poligonos de C4D -------------------------
    {
        bool smooth = true;
        const c4d::PolySoup cube = c4d::fromIronMesh(makeCubeMesh(), &smooth);
        check(cube.points.size() == 8, "exportar cubo: 8 puntos");
        check(cube.polys.size() == 6, "exportar cubo: 6 cuadrilateros");
        bool quads = true;
        for (const auto& p : cube.polys) quads = quads && p[2] != p[3];
        check(quads, "exportar cubo: todo cuadrilateros");
        check(!smooth, "exportar cubo: caras planas");
        // Ida y vuelta: las caras siguen mirando hacia fuera y la caja no cambia.
        const MeshData back = c4d::toIronMesh(cube, -1.0);
        check(facesPointOut(back, fromCenter), "exportar cubo: caras hacia fuera tras ida y vuelta");
        check(nearly(back.boundsMin, glm::vec3(-0.5f)) && nearly(back.boundsMax, glm::vec3(0.5f)),
              "exportar cubo: caja");

        const c4d::PolySoup plane = c4d::fromIronMesh(makePlaneMesh(), nullptr);
        check(plane.points.size() == 4 && plane.polys.size() == 1, "exportar plano: 1 cuadrilatero");
        const MeshData planeBack = c4d::toIronMesh(plane, -1.0);
        check(facesPointOut(planeBack, [](const glm::vec3&) { return glm::vec3(0.0f, 1.0f, 0.0f); }),
              "exportar plano: sigue mirando a +Y");

        const c4d::PolySoup sphere = c4d::fromIronMesh(c4d::toIronMesh(c4d::makeSphere(100.0, 24), kPi80()), &smooth);
        check(smooth, "exportar esfera: normales suavizadas");
        const MeshData sphereBack = c4d::toIronMesh(sphere, -1.0);
        check(facesPointOut(sphereBack, fromCenter), "exportar esfera: caras hacia fuera");
        check(nearly(sphereBack.boundsMax, glm::vec3(0.5f), 1e-3f), "exportar esfera: radio");
    }

    // --- Miniaturas (rasterizador por CPU) ------------------------------------
    {
        std::vector<Material> mats(1);
        mats[0].baseColor = glm::vec3(0.9f, 0.1f, 0.1f);
        SceneNode cube("Cubo", PrimitiveType::Cube);
        cube.materialIndex = 0;
        const Thumbnail th = renderThumbnail({&cube}, mats, 64);
        check(th.width == 64 && th.height == 64 && th.rgba.size() == 64u * 64u * 4u, "miniatura: tamaño");
        check(th.triangles == 12, "miniatura: 12 triangulos del cubo");
        const uint8_t* c = &th.rgba[(32u * 64u + 32u) * 4u];
        check(c[0] > c[1] + 40 && c[0] > c[2] + 40 && c[3] == 255, "miniatura: el centro es el cubo rojo");
        const uint8_t* corner = &th.rgba[0];
        check(std::abs((int)corner[0] - (int)corner[2]) < 4, "miniatura: la esquina es fondo gris");

        SceneNode hidden("Oculto", PrimitiveType::Sphere);
        hidden.editorVis = VisMode::Off;
        check(renderThumbnail({&hidden}, mats, 32).triangles == 0, "miniatura: lo oculto no se dibuja");
        check(renderThumbnail({}, mats, 32).rgba.size() == 32u * 32u * 4u, "miniatura: vacia, solo fondo");
    }

    // --- Reparentado (Agrupar / Expandir Grupo): setLocalMatrix y mundo ------
    {
        auto sameMat = [](const glm::mat4& a, const glm::mat4& b) {
            for (int c = 0; c < 4; ++c)
                if (glm::length(a[c] - b[c]) > 1e-3f) return false;
            return true;
        };
        SceneNode n("n");
        n.position = {1.5f, -2.0f, 3.0f};
        n.rotationDeg = {20.0f, 10.0f, 30.0f};
        n.scale = {1.0f, 2.0f, 0.5f};
        const glm::mat4 m = n.localMatrix();
        SceneNode back("b");
        back.setLocalMatrix(m);
        check(sameMat(back.localMatrix(), m), "setLocalMatrix: misma matriz");
        check(nearly(back.rotationDeg, n.rotationDeg, 1e-2f), "setLocalMatrix: mismos angulos");
        SceneNode gimbal("g");
        gimbal.rotationDeg = {0.0f, 90.0f, 40.0f};
        SceneNode g2("g2");
        g2.setLocalMatrix(gimbal.localMatrix());
        check(sameMat(g2.localMatrix(), gimbal.localMatrix()), "setLocalMatrix: bloqueo de cardan");
        // En HPB el bloqueo de cardan es con P = +-90 (rotationDeg.x).
        SceneNode gp("gp");
        gp.rotationDeg = {90.0f, 35.0f, 20.0f};
        SceneNode gp2("gp2");
        gp2.setLocalMatrix(gp.localMatrix());
        check(sameMat(gp2.localMatrix(), gp.localMatrix()), "HPB: bloqueo de cardan con P = 90");

        // Orden HPB de C4D: R = Ry(H) * Rx(P) * Rz(B) (primero Banco, luego Pitch, luego Heading).
        {
            const glm::vec3 hpb(30.0f, 45.0f, 60.0f);  // P, H, B
            const glm::mat4 ry = glm::rotate(glm::mat4(1.0f), glm::radians(45.0f), glm::vec3(0, 1, 0));
            const glm::mat4 rx = glm::rotate(glm::mat4(1.0f), glm::radians(30.0f), glm::vec3(1, 0, 0));
            const glm::mat4 rz = glm::rotate(glm::mat4(1.0f), glm::radians(60.0f), glm::vec3(0, 0, 1));
            check(sameMat(glm::mat4(hpbToMat3(hpb)), ry * rx * rz), "HPB: orden Ry * Rx * Rz");
            check(nearly(mat3ToHpb(hpbToMat3(hpb)), hpb, 1e-3f), "HPB: ida y vuelta");
            // Escena vieja (orden Rz*Ry*Rx): misma orientacion tras convertir.
            const glm::vec3 old(20.0f, 10.0f, 30.0f);
            const glm::vec3 conv = mat3ToHpb(legacyXyzToMat3(old));
            check(sameMat(glm::mat4(hpbToMat3(conv)), glm::mat4(legacyXyzToMat3(old))), "HPB: conversion de escenas v3");
        }

        // Agrupar bajo un padre girado y volver a sacar: el hijo no se mueve en mundo.
        SceneNode root("root");
        SceneNode* parent = root.addChild("padre");
        parent->position = {5.0f, 0.0f, 0.0f};
        parent->rotationDeg = {0.0f, 45.0f, 0.0f};
        parent->scale = glm::vec3(2.0f);
        SceneNode* kid = root.addChild("hijo", PrimitiveType::Cube);
        kid->position = {1.0f, 2.0f, 3.0f};
        kid->rotationDeg = {10.0f, 0.0f, 5.0f};
        const glm::mat4 world = kid->worldMatrix();
        const glm::mat4 local = glm::inverse(parent->worldMatrix()) * world;
        SceneNode* moved = parent->insertChild(root.detachChild(kid), 0);
        moved->setLocalMatrix(local);
        check(moved->parent() == parent && root.children().size() == 1, "insertChild/detachChild: arbol");
        check(sameMat(moved->worldMatrix(), world), "reparentado: mismo mundo");
        check(parent->indexOf(moved) == 0 && root.indexOf(moved) == root.children().size(), "indexOf");
    }

    {
        // Render de camara (Mayus+R): un suelo grande que pasa por detras de
        // la camara se recorta en el plano cercano en vez de desaparecer.
        SceneNode root("root");
        SceneNode* floor = root.addChild("Suelo", PrimitiveType::Plane);
        floor->scale = glm::vec3(50.0f);
        const glm::mat4 view = glm::lookAt(glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.0f, 1.0f, -5.0f), glm::vec3(0, 1, 0));
        const glm::mat4 proj = glm::perspective(glm::radians(50.0f), 4.0f / 3.0f, 0.05f, 100.0f);
        const Thumbnail img = renderCameraImage(root, {}, view, proj, 64, 48, 1, glm::vec3(0.0f), glm::vec3(0.0f));
        check(img.width == 64 && img.height == 48 && img.rgba.size() == 64u * 48u * 4u, "render de camara: tamano");
        check(img.triangles > 0, "render de camara: suelo recortado (no descartado)");
        const uint8_t* bottom = &img.rgba[((size_t)47 * 64 + 32) * 4];
        const uint8_t* top = &img.rgba[((size_t)0 * 64 + 32) * 4];
        check(bottom[0] > 0, "render de camara: suelo visible abajo");
        check(top[0] == 0, "render de camara: fondo arriba");
        floor->renderVis = VisMode::Off;
        const Thumbnail hidden = renderCameraImage(root, {}, view, proj, 32, 24, 1, glm::vec3(0.0f), glm::vec3(0.0f));
        check(hidden.triangles == 0, "render de camara: respeta la visibilidad de render");
    }

    {
        // Textura de Color en el render por CPU: PNG de 8x1 (4 rojos | 4 azules)
        // sobre un cuadrado con UV 0..1, visto desde arriba.
        const std::filesystem::path dir = std::filesystem::temp_directory_path() / "iron3d_tex_test";
        std::filesystem::create_directories(dir);
        {
            unsigned char px[24] = {};
            for (int i = 0; i < 8; ++i) px[i * 3 + (i < 4 ? 0 : 2)] = 255;
            std::ofstream f(dir / "rojo_azul.png", std::ios::binary | std::ios::trunc);
            stbi_write_png_to_func(
                [](void* ctx, void* data, int size) { static_cast<std::ofstream*>(ctx)->write((const char*)data, size); },
                &f, 8, 1, 3, px, 24);
        }
        SceneNode root("root");
        SceneNode* sq = root.addChild("Cuadrado", PrimitiveType::Mesh);
        MeshData quad = testio::uvMesh();
        quad.indices.resize(6); // solo el cuadrado
        sq->mesh = std::make_shared<MeshData>(quad);
        sq->materialIndex = 0;
        Material m;
        m.baseColor = glm::vec3(0.5f);
        m.roughness = 1.0f;
        m.texture(TexSlot::Color) = "rojo_azul.png";
        m.texBaseDir = pathUtf8(dir);
        std::vector<Material> mats{m};
        const glm::mat4 view = glm::lookAt(glm::vec3(0.0f, 3.0f, 0.0f), glm::vec3(0.0f), glm::vec3(0, 0, -1));
        const glm::mat4 proj = glm::ortho(-0.5f, 0.5f, -0.5f, 0.5f, 0.1f, 10.0f);
        auto px = [](const Thumbnail& t, int x, int y) { return &t.rgba[((size_t)y * (size_t)t.width + (size_t)x) * 4]; };
        const Thumbnail img = renderCameraImage(root, mats, view, proj, 32, 32, 1, glm::vec3(0.0f), glm::vec3(0.0f));
        const uint8_t *l = px(img, 4, 16), *r = px(img, 27, 16);
        check(l[0] > 2 * l[2] && r[2] > 2 * r[0], "textura: render por CPU, rojo a la izquierda (u=0) y azul a la derecha");
        // Mosaico de la etiqueta: longitud 0.5 -> dos repeticiones.
        sq->texTag.length = glm::vec2(0.5f, 1.0f);
        const Thumbnail tiled = renderCameraImage(root, mats, view, proj, 32, 32, 1, glm::vec3(0.0f), glm::vec3(0.0f));
        const uint8_t *t1 = px(tiled, 2, 16), *t2 = px(tiled, 10, 16), *t3 = px(tiled, 18, 16);
        check(t1[0] > 2 * t1[2] && t2[2] > 2 * t2[0] && t3[0] > 2 * t3[2], "textura: la etiqueta repite (longitud U 0.5)");
        // Sin archivo: color base.
        mats[0].texture(TexSlot::Color) = "no_existe.png";
        sq->texTag = TextureTagData{};
        const Thumbnail plain = renderCameraImage(root, mats, view, proj, 32, 32, 1, glm::vec3(0.0f), glm::vec3(0.0f));
        const uint8_t* c = px(plain, 16, 16);
        check(std::abs((int)c[0] - (int)c[2]) <= 2, "textura: si no esta en disco se usa el color base");
    }

    {
        // Animacion (F9): pistas por canal, interpolacion sin rebasar y .iron.
        SceneNode root("root");
        SceneNode* n = root.addChild("Cubo", PrimitiveType::Cube);
        n->position = {0.0f, 0.0f, 0.0f};
        recordKey(*n, 0, true, true, true);
        n->position = {10.0f, 0.0f, 0.0f};
        n->rotationDeg = {0.0f, 90.0f, 0.0f};
        recordKey(*n, 10, true, false, false);  // solo Posicion
        n->position = {10.0f, 5.0f, 0.0f};
        recordKey(*n, 20, true, false, false);
        check(n->animPos.keys.size() == 3 && n->animRot.keys.size() == 1 && n->animScale.keys.size() == 1,
              "animacion: claves por canal");
        evaluateAnimation(root, 5.0f);
        check(std::fabs(n->position.x - 5.0f) < 1e-4f, "animacion: dos claves en S, mitad exacta");
        check(n->rotationDeg.y == 0.0f, "animacion: rotacion sin grabar en 10 sigue en su unica clave");
        evaluateAnimation(root, 15.0f);
        check(n->position.x <= 10.0f + 1e-4f, "animacion: sin rebasar la clave (extremo recortado)");
        check(n->position.y > 0.0f && n->position.y < 5.0f, "animacion: interpola entre 10 y 20");
        evaluateAnimation(root, -5.0f);
        check(n->position.x == 0.0f, "animacion: antes de la primera clave, fija");
        evaluateAnimation(root, 99.0f);
        check(nearly(n->position, {10.0f, 5.0f, 0.0f}), "animacion: despues de la ultima, fija");
        const std::vector<int> kf = keyFrames(*n);
        check(kf.size() == 3 && kf[0] == 0 && kf[1] == 10 && kf[2] == 20, "animacion: keyFrames");
        recordKey(*n, 10, true, false, false);  // sustituye, no duplica
        check(n->animPos.keys.size() == 3, "animacion: grabar dos veces el mismo fotograma");

        const std::string text = serializeScene(root, {});
        SceneNode back("cargada");
        std::vector<Material> mats;
        std::string err;
        check(deserializeScene(text, back, mats, &err), "animacion: el .iron se vuelve a leer");
        const SceneNode* bn = back.children().empty() ? nullptr : back.children().front().get();
        check(bn && bn->animPos.keys.size() == 3 && bn->animRot.keys.size() == 1 && bn->animScale.keys.size() == 1,
              "animacion: claves guardadas y leidas");
        check(bn && bn->animPos.keys[2].frame == 20 && nearly(bn->animPos.keys[2].value, {10.0f, 5.0f, 0.0f}),
              "animacion: valor de la clave tras guardar");
        SceneNode copyParent("p");
        const SceneNode* dup = duplicateSubtree(copyParent, *n);
        check(dup->animPos.keys.size() == 3, "animacion: Duplicar copia las pistas");
    }

    // --- Topologia de componentes (Puntos/Aristas/Poligonos) -------------------
    {
        const MeshData cube = makeCubeMesh();
        const MeshTopology t = buildMeshTopology(cube);
        check(t.points.size() == 8, "topologia cubo: 8 puntos");
        check(t.edges.size() == 12, "topologia cubo: 12 aristas (sin diagonales)");
        check(t.polys.size() == 6, "topologia cubo: 6 poligonos");
        bool quads = true, out = true;
        for (size_t p = 0; p < t.polys.size(); ++p) {
            quads = quads && t.polys[p].size() == 4 && t.polyTris[p].size() == 2;
            out = out && glm::dot(t.polyNormals[p], polyCenter(t, (int)p)) > 0.0f;
        }
        check(quads, "topologia cubo: todo cuadrilateros");
        check(out, "topologia cubo: normales de poligono hacia fuera");

        const MeshTopology pl = buildMeshTopology(makePlaneMesh());
        check(pl.points.size() == 4 && pl.edges.size() == 4 && pl.polys.size() == 1, "topologia plano: 1 cuadrilatero");

        const MeshData sphere = makeSphereMesh();
        const MeshTopology st = buildMeshTopology(sphere);
        check((int)st.points.size() - (int)st.edges.size() + (int)st.polys.size() == 2,
              "topologia esfera: V - A + P = 2 (cerrada, sin diagonales)");

        // Borrar una cara del cubo: 5 poligonos, mismos puntos y aristas.
        std::vector<char> keep(t.polys.size(), 1);
        keep[0] = 0;
        const MeshData open = keepPolygons(cube, t, keep);
        const MeshTopology ot = buildMeshTopology(open);
        check(open.faceTris.size() == 5 && ot.polys.size() == 5, "borrar poligono: quedan 5");
        check(ot.points.size() == 8 && ot.edges.size() == 12, "borrar poligono: 8 puntos y 12 aristas");

        // Pentagono explicito (abanico de 3 triangulos): un solo poligono.
        MeshData pent;
        for (int i = 0; i < 5; ++i) {
            const float a = 6.2831853f * (float)i / 5.0f;
            pent.vertices.push_back({glm::vec3(std::cos(a), 0.0f, -std::sin(a)), glm::vec3(1.0f), glm::vec3(0, 1, 0)});
        }
        pent.indices = {0, 1, 2, 0, 2, 3, 0, 3, 4};
        pent.faceTris = {3};
        const MeshTopology pt = buildMeshTopology(pent);
        check(pt.polys.size() == 1 && pt.polys[0].size() == 5 && pt.edges.size() == 5, "pentagono: 1 poligono de 5 lados");
        pent.faceTris.clear();
        check(buildMeshTopology(pent).polys.size() == 2, "pentagono sin FACES: se deduce cuadrilatero + triangulo");

        // FACES en el .iron (formato 6).
        SceneNode root("raiz");
        SceneNode* node = root.addChild("penta", PrimitiveType::Mesh);
        pent.faceTris = {3};
        node->mesh = std::make_shared<const MeshData>(pent);
        std::vector<Material> mats;
        const std::string text = serializeScene(root, mats);
        SceneNode back("x");
        std::string err;
        check(deserializeScene(text, back, mats, &err), "FACES: el .iron se vuelve a leer");
        const SceneNode* bn = back.children().empty() ? nullptr : back.children().front().get();
        check(bn && bn->mesh && bn->mesh->faceTris == std::vector<uint32_t>{3}, "FACES: poligonos guardados y leidos");

        // Simetria y poligonos ocultos tambien ida y vuelta.
        node->symmetry.present = true;
        node->symmetry.axis = 1;
        node->hiddenPolys = std::make_shared<const std::vector<char>>(std::vector<char>{0, 1, 1});
        node->hiddenFor = node->mesh.get();
        const std::string text2 = serializeScene(root, mats);
        SceneNode back2("x");
        check(deserializeScene(text2, back2, mats, &err), "SYM/HIDDEN: el .iron se vuelve a leer");
        const SceneNode* b2 = back2.children().empty() ? nullptr : back2.children().front().get();
        check(b2 && b2->symmetry.present && b2->symmetry.axis == 1, "SYM: simetria guardada y leida");
        check(b2 && b2->hasHidden() && b2->hiddenPolys->size() == 3 && !(*b2->hiddenPolys)[0] && (*b2->hiddenPolys)[1] &&
                  (*b2->hiddenPolys)[2],
              "HIDDEN: poligonos ocultos guardados y leidos");
    }

    // --- Cerrar Agujero con agujeros concavos (orejas) -------------------------
    {
        const glm::vec2 outline[6] = {{0, 0}, {2, 0}, {2, 1}, {1, 1}, {1, 2}, {0, 2}};  // L, antihoraria
        MeshData m;
        for (int i = 0; i < 6; ++i) {
            const glm::vec2 a = outline[i], b = outline[(i + 1) % 6];
            const glm::vec3 d = glm::normalize(glm::vec3(b.x - a.x, b.y - a.y, 0.0f));
            const glm::vec3 nrm = glm::cross(d, glm::vec3(0, 0, 1));  // hacia fuera
            const uint32_t base = (uint32_t)m.vertices.size();
            m.vertices.push_back({glm::vec3(a.x, a.y, 0), glm::vec3(0.8f), nrm});
            m.vertices.push_back({glm::vec3(b.x, b.y, 0), glm::vec3(0.8f), nrm});
            m.vertices.push_back({glm::vec3(b.x, b.y, 1), glm::vec3(0.8f), nrm});
            m.vertices.push_back({glm::vec3(a.x, a.y, 1), glm::vec3(0.8f), nrm});
            m.indices.insert(m.indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
            m.faceTris.push_back(2);
        }
        updateBounds(m);
        const MeshTopology t0 = buildMeshTopology(m);
        check(t0.polys.size() == 6, "agujero en L: 6 paredes");
        const MeshEditResult closed = closeHoles(m, t0, {});
        const MeshTopology t1 = buildMeshTopology(closed.mesh);
        // 6 paredes + 2 tapas en L de 4 triangulos cada una
        check(t1.polys.size() == 14, "agujero en L: las dos tapas concavas se cierran con 4 triangulos cada una");
        std::set<std::pair<int, int>> dir;
        for (const auto& pl : t1.polys)
            for (size_t k = 0; k < pl.size(); ++k) dir.insert({pl[k], pl[(k + 1) % pl.size()]});
        bool sealed = true;
        for (const auto& [a, b] : dir) sealed = sealed && dir.count({b, a}) > 0;
        check(sealed, "agujero en L: malla cerrada y con las caras bien orientadas");
    }

    // --- Visibilidad de componentes (Solo Seleccionar Visibles) ---------------
    {
        const MeshData cube = makeCubeMesh();
        const MeshTopology t = buildMeshTopology(cube);
        const glm::mat4 view = glm::lookAt(glm::vec3(0.0f, 0.0f, 3.0f), glm::vec3(0.0f), glm::vec3(0, 1, 0));
        const glm::mat4 proj = glm::perspective(glm::radians(40.0f), 1.0f, 0.1f, 100.0f);
        const ComponentVisibility v = computeComponentVisibility(cube, t, proj * view, 400, 400);
        int visPolys = 0, frontVis = 0, backVis = 0;
        for (size_t p = 0; p < t.polys.size(); ++p) {
            visPolys += v.polys[p];
            if (t.polyNormals[p].z > 0.9f) frontVis = v.polys[p];
            if (t.polyNormals[p].z < -0.9f) backVis = v.polys[p];
        }
        check(frontVis == 1 && backVis == 0, "visibilidad: la cara de delante se ve, la de detras no");
        int visPts = 0;
        for (size_t i = 0; i < t.points.size(); ++i) visPts += v.points[i];
        check(visPts == 4, "visibilidad: de frente se ven 4 puntos del cubo");
        (void)visPolys;
        // Ocultar la cara de delante: ya no tapa, y la de detras pasa a verse.
        std::vector<char> hid(t.polys.size(), 0);
        for (size_t p = 0; p < t.polys.size(); ++p)
            if (t.polyNormals[p].z > 0.9f) hid[p] = 1;
        const ComponentVisibility vh = computeComponentVisibility(cube, t, proj * view, 400, 400, &hid);
        int frontVisH = 1, backVisH = 0;
        for (size_t p = 0; p < t.polys.size(); ++p) {
            if (t.polyNormals[p].z > 0.9f) frontVisH = vh.polys[p];
            if (t.polyNormals[p].z < -0.9f) backVisH = vh.polys[p];
        }
        check(frontVisH == 0 && backVisH == 1, "ocultar: la cara oculta no se ve y deja de tapar a la de detras");
    }


    // --- Herramientas de modelado (Extruir, Biselar, Cuchillo) ------------------
    {
        const MeshData cube = makeCubeMesh();
        const MeshTopology t = buildMeshTopology(cube);
        int top = -1;
        for (size_t p = 0; p < t.polyNormals.size(); ++p)
            if (t.polyNormals[p].y > 0.9f) top = (int)p;
        check(top >= 0, "cubo: hay cara superior");

        // Extruir la cara de arriba 0.5: 12 puntos, 20 aristas, 10 poligonos;
        // la tapa sube a y = 1 y queda seleccionada.
        const MeshEditResult e = extrudePolygons(cube, t, {top}, 0.5f);
        const MeshTopology et = buildMeshTopology(e.mesh);
        check(e.changed == 1, "extruir: 1 poligono");
        check(et.points.size() == 12 && et.edges.size() == 20 && et.polys.size() == 10,
              "extruir cara del cubo: 12 puntos, 20 aristas, 10 poligonos");
        const std::set<int> cap = polysFromTris(et, e.selectTris);
        check(cap.size() == 1 && std::fabs(polyCenter(et, *cap.begin()).y - 1.0f) < 1e-5f, "extruir: la tapa sube y queda seleccionada");
        bool outward = true;
        for (size_t p = 0; p < et.polys.size(); ++p) {
            const glm::vec3 c = polyCenter(et, (int)p) - glm::vec3(0.0f, 0.25f, 0.0f);
            outward = outward && glm::dot(et.polyNormals[p], c) > 0.0f;
        }
        check(outward, "extruir: paredes y tapa hacia fuera");
        check((int)et.points.size() - (int)et.edges.size() + (int)et.polys.size() == 2, "extruir: sigue cerrada (V - A + P = 2)");

        // Extruir dos caras vecinas como region: sin pared entre ellas.
        int front = -1;
        for (size_t p = 0; p < t.polyNormals.size(); ++p)
            if (t.polyNormals[p].z > 0.9f) front = (int)p;
        const MeshTopology rt = buildMeshTopology(extrudePolygons(cube, t, {top, front}, 0.25f).mesh);
        check(rt.polys.size() == 6 - 2 + 2 + 6, "extruir region de 2 caras: 6 paredes, 2 tapas");

        // Biselar (insercion 0.1 + extrusion 0.2): el cuadrado de arriba da
        // 1 interior + 4 del anillo.
        const MeshEditResult b = bevelPolygons(cube, t, {top}, 0.1f, 0.2f);
        const MeshTopology bt = buildMeshTopology(b.mesh);
        check(bt.polys.size() == 10 && bt.points.size() == 12, "biselar cara del cubo: 10 poligonos, 12 puntos");
        const std::set<int> inner = polysFromTris(bt, b.selectTris);
        check(inner.size() == 1 && std::fabs(polyCenter(bt, *inner.begin()).y - 0.7f) < 1e-5f,
              "biselar: el interior sube 0.2 y queda seleccionado");
        check((int)bt.points.size() - (int)bt.edges.size() + (int)bt.polys.size() == 2, "biselar: sigue cerrada");

        // Cuchillo: plano x = 0 corta las 4 caras que lo cruzan (no las de x = +-0.5).
        const MeshEditResult k = knifeCut(cube, t, glm::vec3(0.0f), glm::vec3(1.0f, 0.0f, 0.0f), {}, nullptr);
        const MeshTopology kt = buildMeshTopology(k.mesh);
        check(k.changed == 4, "cuchillo: 4 caras cortadas");
        check(kt.polys.size() == 10 && kt.points.size() == 12 && kt.edges.size() == 20,
              "cuchillo: 10 poligonos, 12 puntos, 20 aristas");
        check((int)kt.points.size() - (int)kt.edges.size() + (int)kt.polys.size() == 2, "cuchillo: sigue cerrada");
        std::vector<char> only(t.polys.size(), 0);
        only[(size_t)top] = 1;
        check(knifeCut(cube, t, glm::vec3(0.0f), glm::vec3(1.0f, 0.0f, 0.0f), only, nullptr).changed == 1,
              "cuchillo: solo los poligonos permitidos");
        check(knifeCut(cube, t, glm::vec3(0.0f), glm::vec3(1.0f, 0.0f, 0.0f), {},
                       [](const glm::vec3& p) { return p.y > 0.0f; })
                      .changed == 1,
              "cuchillo: solo donde los dos cortes caen en el tramo");
    }


    // --- Gizmo: solo los handles de verdad cuentan como handle (pickHandle) ---
    {
        SceneNode n("cubo");
        const glm::vec3 eye(0.0f, 0.0f, 5.0f);
        const int W = 800, H = 600;
        const glm::mat4 view = glm::lookAt(eye, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        const glm::mat4 proj = glm::perspective(glm::radians(45.0f), (float)W / (float)H, 0.1f, 100.0f);
        const glm::mat4 vp = proj * view;
        auto pickAt = [&](TransformGizmo& gz, double x, double y) {
            const Ray r = TransformGizmo::screenRay(x, y, W, H, view, proj, eye);
            return gz.pickHandle(r, &n, eye, x, y, W, H, vp);
        };
        TransformGizmo gz;
        gz.mode = GizmoMode::Translate;
        // Lejos de las flechas: no es handle, aunque beginDrag lo tomaria como Free.
        check(pickAt(gz, 650.0, 120.0) == GizmoAxis::None, "gizmo: clic lejos de las flechas no es handle");
        {
            const Ray r = TransformGizmo::screenRay(650.0, 120.0, W, H, view, proj, eye);
            check(gz.beginDrag(r, &n, eye, 650.0, 120.0, W, H, vp, glm::vec3(1, 0, 0), glm::vec3(0, 1, 0)),
                  "gizmo: beginDrag sigue aceptando el clic como Free");
            gz.endDrag();
        }
        bool sawX = false;
        for (int dx = 4; dx < 300 && !sawX; dx += 2) sawX = pickAt(gz, 400.0 + dx, 300.0) == GizmoAxis::X;
        check(sawX, "gizmo: la flecha X es handle");
        // Rotar: el centro (Free) no es handle; el aro Z y el aro de vista si; muy lejos, nada.
        gz.mode = GizmoMode::Rotate;
        check(pickAt(gz, 400.0, 300.0) == GizmoAxis::None, "gizmo: el centro de Rotar no es handle");
        check(pickAt(gz, 790.0, 10.0) == GizmoAxis::None, "gizmo: lejos del aro de vista no es handle");
        bool sawZ = false, sawView = false;
        for (int d = 2; d < 280; d += 1) {
            const GizmoAxis a = pickAt(gz, 400.0 + d * 0.7071, 300.0 - d * 0.7071);
            sawZ = sawZ || a == GizmoAxis::Z;
            sawView = sawView || a == GizmoAxis::View;
        }
        check(sawZ && sawView, "gizmo: aro Z y aro de vista son handles");
        // Forzar Free arranca el arrastre aunque el clic no caiga en nada.
        {
            const Ray r = TransformGizmo::screenRay(400.0, 300.0, W, H, view, proj, eye);
            check(gz.beginDrag(r, &n, eye, 400.0, 300.0, W, H, vp, glm::vec3(1, 0, 0), glm::vec3(0, 1, 0),
                               GizmoAxis::Free) &&
                      gz.isDragging(),
                  "gizmo: arrastre Free forzado");
            gz.endDrag();
        }
    }


    // --- Edicion de claves: mover, borrar, interpolacion y .iron v6 ------------
    {
        SceneNode root("root");
        SceneNode* n = root.addChild("animado");
        n->animPos.set(0, {0.0f, 0.0f, 0.0f});
        n->animPos.set(10, {10.0f, 0.0f, 0.0f});
        n->animPos.set(20, {10.0f, 10.0f, 0.0f});
        n->animRot.set(10, {0.0f, 45.0f, 0.0f});

        // Interpolacion por tramo: Lineal da el punto medio exacto, Escalon se queda.
        check(setKeyInterp(*n, {0}, KeyInterp::Linear) == 1, "claves: interpolacion en 1 clave");
        check(nearly(n->animPos.evaluate(5.0f), {5.0f, 0.0f, 0.0f}), "claves: tramo Lineal = punto medio");
        check(setKeyInterp(*n, {10}, KeyInterp::Step) == 2, "claves: interpolacion en las dos pistas del fotograma 10");
        check(nearly(n->animPos.evaluate(19.0f), {10.0f, 0.0f, 0.0f}), "claves: tramo Escalon se queda hasta la siguiente");
        check(nearly(n->animPos.evaluate(20.0f), {10.0f, 10.0f, 0.0f}), "claves: Escalon llega a la clave siguiente");

        // Mover: el fotograma 10 a 15 (Pos y Rot), conservando la interpolacion.
        check(moveKeys(*n, {10}, 5) == 2, "claves: mover 2 claves (2 pistas)");
        check(n->animPos.hasKeyAt(15) && !n->animPos.hasKeyAt(10) && n->animRot.hasKeyAt(15), "claves: movidas a 15");
        check(n->animPos.keys[1].interp == KeyInterp::Step, "claves: al mover se conserva la interpolacion");
        // Mover encima de otra la sustituye.
        check(moveKeys(*n, {15}, 5) == 2 && n->animPos.keys.size() == 2 && n->animPos.keys[1].frame == 20 &&
                  nearly(n->animPos.keys[1].value, {10.0f, 0.0f, 0.0f}),
              "claves: la movida sustituye a la que estaba");
        check(keyFrames(*n) == std::vector<int>{0, 20}, "claves: fotogramas tras mover");

        // .iron v6 con i=
        std::vector<Material> mats;
        const std::string text = serializeScene(root, mats);
        check(text.find(" i=s") != std::string::npos && text.find(" i=l") != std::string::npos, "claves: se guarda i=l / i=s");
        SceneNode back("x");
        std::string err;
        check(deserializeScene(text, back, mats, &err), "claves: el .iron v6 se vuelve a leer");
        const SceneNode* bn = back.children().empty() ? nullptr : back.children().front().get();
        check(bn && bn->animPos.keys.size() == 2 && bn->animPos.keys[0].interp == KeyInterp::Linear &&
                  bn->animPos.keys[1].interp == KeyInterp::Step,
              "claves: interpolacion guardada y leida");

        check(deleteKeys(*n, {0, 20}) == 3 && n->animPos.empty() && n->animRot.empty(), "claves: borrar todas");
    }


    // --- Seleccion por topologia y Soldar / Cerrar Agujero / Subdividir / Invertir ---
    {
        const MeshData cube = makeCubeMesh();
        const MeshTopology t = buildMeshTopology(cube);
        int top = -1;
        for (size_t p = 0; p < t.polyNormals.size(); ++p)
            if (t.polyNormals[p].y > 0.9f) top = (int)p;
        auto euler = [](const MeshTopology& m) { return (int)m.points.size() - (int)m.edges.size() + (int)m.polys.size(); };

        // Anillo en el cubo: las 4 aristas paralelas. Bucle: los puntos tienen 3 aristas, se queda en 1.
        check(edgeRing(t, 0).size() == 4, "anillo: 4 aristas paralelas en el cubo");
        check(edgeLoop(t, 0).size() == 1, "bucle: en el cubo no sigue (puntos de 3 aristas)");
        // Esfera 12x18: el ecuador es un bucle de 18 aristas y su anillo vertical tambien.
        const MeshTopology st = buildMeshTopology(makeSphereMesh());
        int eq = -1, vert = -1;
        for (size_t e = 0; e < st.edges.size(); ++e) {
            const float ya = st.points[(size_t)st.edges[e].first].y, yb = st.points[(size_t)st.edges[e].second].y;
            if (std::fabs(ya) < 1e-4f && std::fabs(yb) < 1e-4f) eq = (int)e;
            if ((std::fabs(ya) < 1e-4f) != (std::fabs(yb) < 1e-4f) && std::max(std::fabs(ya), std::fabs(yb)) < 0.4f) vert = (int)e;
        }
        check(eq >= 0 && edgeLoop(st, eq).size() == 18, "bucle: ecuador de la esfera = 18 aristas");
        check(vert >= 0 && edgeRing(st, vert).size() == 18, "anillo: 18 aristas verticales alrededor");
        check(eq >= 0 && edgeRing(st, eq).size() > 2, "anillo del ecuador: recorre los meridianos");

        // Cuchillo en Bucle: un anillo del cubo (4 aristas, 4 caras) -> 4 caras mas, 4 puntos nuevos, malla cerrada.
        {
            const MeshEditResult lc = loopCut(cube, t, 0, 0.5f);
            const MeshTopology lt = buildMeshTopology(lc.mesh);
            check(lc.changed == 4, "bucle cortado: 4 caras partidas");
            check(lt.polys.size() == 10 && lt.points.size() == 12 && lt.edges.size() == 20, "bucle cortado: 10 caras, 12 puntos, 20 aristas");
            check(euler(lt) == 2, "bucle cortado: sigue siendo una malla cerrada (Euler 2)");
            check(checkMesh(lt).clean(), "bucle cortado: sin bordes abiertos");
            check(loopCut(cube, t, -1).changed == 0, "bucle cortado: arista invalida no hace nada");
            // En la esfera (polos triangulares) un anillo vertical solo parte los cuadrilateros.
            check(vert >= 0 && loopCut(makeSphereMesh(), st, vert, 0.3f).changed > 0, "bucle cortado: esfera");
        }

        // Primitivas poligonales: cerradas y orientadas hacia fuera (salvo el Disco, que es plano y abierto).
        {
            struct Prim { const char* name; MeshData m; bool closed; };
            std::vector<Prim> prims;
            prims.push_back({"cilindro", makeCylinderMesh(), true});
            prims.push_back({"cono", makeConeMesh(), true});
            prims.push_back({"toroide", makeTorusMesh(), true});
            prims.push_back({"capsula", makeCapsuleMesh(), true});
            prims.push_back({"tubo", makeTubeMesh(), true});
            prims.push_back({"piramide", makePyramidMesh(), true});
            for (int k = 0; k < 4; ++k) prims.push_back({"platonico", makePlatonicMesh(k), true});
            prims.push_back({"disco", makeDiscMesh(), false});
            prims.push_back({"disco con agujero", makeDiscMesh(0.5f, 0.2f), false});
            for (const Prim& pr : prims) {
                const MeshTopology pt = buildMeshTopology(pr.m);
                const std::string nm = std::string("primitiva ") + pr.name;
                check(!pt.polys.empty() && !pr.m.indices.empty(), (nm + ": tiene poligonos").c_str());
                if (!pr.closed) continue;
                check(checkMesh(pt).clean(), (nm + ": malla cerrada y limpia").c_str());
                // orientacion: la normal geometrica de cada triangulo coincide con la normal de sus vertices
                int out = 0, total = 0;
                for (size_t i = 0; i + 2 < pr.m.indices.size(); i += 3) {
                    const glm::vec3 a0 = pr.m.vertices[pr.m.indices[i]].position, b0 = pr.m.vertices[pr.m.indices[i + 1]].position,
                                    c0 = pr.m.vertices[pr.m.indices[i + 2]].position;
                    const glm::vec3 n = glm::cross(b0 - a0, c0 - a0);
                    ++total;
                    if (glm::dot(n, pr.m.vertices[pr.m.indices[i]].normal) > 0.0f) ++out;
                }
                check(out * 10 >= total * 9, (nm + ": orientada hacia fuera").c_str());
            }
        }

        // Primitivas paramétricas: cada tipo genera una malla valida y los parametros la cambian.
        {
            for (int k = 1; k <= 8; ++k) {
                const ParamShape ps = defaultParamShape(k);
                const MeshData pm = buildParamShape(ps);
                const std::string nm = "parametrica " + std::to_string(k);
                check(!pm.indices.empty(), (nm + ": genera malla").c_str());
                if (k != 3) check(checkMesh(buildMeshTopology(pm)).clean(), (nm + ": cerrada").c_str());
                ParamShape big = ps;
                big.a *= 2.0f;
                big.seg1 = k == 8 ? 3 : ps.seg1 + 8;
                const MeshData bm = buildParamShape(big);
                check(bm.boundsMax != pm.boundsMax || bm.indices.size() != pm.indices.size(), (nm + ": los parametros cambian la malla").c_str());
            }
            check(buildParamShape(defaultParamShape(0)).indices.empty(), "parametrica 0: sin malla");
        }

        // Deformadores: el cubo unidad (de -0.5 a 0.5) deformado a lo largo de Y.
        {
            const MeshData cb = makeCubeMesh();
            const glm::mat4 id(1.0f);
            auto maxAbsAtTop = [](const MeshData& m) {
                float mx = 0.0f;
                for (const Vertex& v : m.vertices)
                    if (v.position.y > 0.499f) mx = std::max(mx, std::max(std::fabs(v.position.x), std::fabs(v.position.z)));
                return mx;
            };
            check(maxAbsAtTop(deformMeshData(cb, 2, 1, 100.0f, id)) < 1e-4f, "afilar 100 %: la cara de arriba es un punto");
            check(maxAbsAtTop(deformMeshData(cb, 2, 1, 0.0f, id)) > 0.49f, "afilar 0 %: no cambia");
            const MeshData tw = deformMeshData(cb, 1, 1, 90.0f, id);
            float topR = 0.0f;
            bool finite = true;
            for (const Vertex& v : tw.vertices) {
                if (v.position.y > 0.499f) topR = std::max(topR, std::hypot(v.position.x, v.position.z));
                finite = finite && std::isfinite(v.position.x + v.position.y + v.position.z + v.normal.x + v.normal.y + v.normal.z);
            }
            check(finite && std::fabs(topR - 0.7071f) < 1e-3f, "enroscar: conserva el radio y gira la cara de arriba");
            const MeshData bd = deformMeshData(cb, 0, 1, 90.0f, id);
            bool bfinite = bd.vertices.size() == cb.vertices.size() && bd.indices == cb.indices;
            for (const Vertex& v : bd.vertices) bfinite = bfinite && std::isfinite(v.position.x + v.position.y + v.position.z);
            check(bfinite && std::fabs(bd.boundsMax.z - 0.5403f) < 2e-3f && std::fabs(bd.boundsMax.y - 0.8037f) < 2e-3f, "doblar 90: malla finita y curvada hacia +Z (esquinas segun la formula del arco)");
            const MeshData bg = deformMeshData(makeSphereMesh(), 3, 1, 50.0f, id);
            check(bg.boundsMax.x > 0.7f && std::fabs(bg.boundsMax.y - 0.5f) < 1e-4f, "hinchar: ensancha el medio sin mover los extremos");
            const MeshData sh = deformMeshData(cb, 4, 1, 100.0f, id);
            check(std::fabs(sh.boundsMax.z - 1.0f) < 1e-4f && std::fabs(sh.boundsMin.z + 1.0f) < 1e-4f, "sesgar 100 %: desplaza los extremos +-0.5 en el eje b");

            SceneNode root("r");
            SceneNode* dn = root.addChild("Doblar", PrimitiveType::None);
            dn->deform.present = true;
            dn->deform.kind = 2;
            dn->deform.amount = 100.0f;
            check(evaluateDeformers(root) == 0 && !dn->mesh, "deformador sin hijo: no hace nada");
            SceneNode* ch = dn->addChild("Cubo", PrimitiveType::Mesh);
            ch->mesh = std::make_shared<const MeshData>(cb);
            check(evaluateDeformers(root) == 1 && dn->mesh && maxAbsAtTop(*dn->mesh) < 1e-4f, "deformador con hijo: el nodo recibe la malla deformada");
            check(evaluateDeformers(root) == 0, "deformador: sin cambios no recalcula");
            dn->deform.amount = 0.0f;
            check(evaluateDeformers(root) == 1, "deformador: al cambiar el parametro se recalcula");
            std::vector<Material> mats;
            const std::string txt = serializeScene(root, mats);
            SceneNode back("x");
            std::string err;
            check(deserializeScene(txt, back, mats, &err), "DEF: el .iron se vuelve a leer");
            const SceneNode* bn = back.children().empty() ? nullptr : back.children().front().get();
            check(bn && bn->deform.present && bn->deform.kind == 2 && bn->deform.axis == 1 && bn->deform.amount == 0.0f, "DEF: deformador guardado y leido");
        }

        // Clonador: cantidades, posiciones, escala y serializacion.
        {
            MeshData cube = makeCubeMesh();
            CloneData cl;
            cl.present = true;
            cl.mode = 0; cl.count = 4; cl.offset = glm::vec3(200.0f, 0.0f, 0.0f);  // 1 unidad por copia
            MeshData o = cloneMeshData(cube, cl, glm::mat4(1.0f));
            check(o.vertices.size() == cube.vertices.size() * 4 && o.indices.size() == cube.indices.size() * 4, "clonador: 4 copias lineales");
            check(o.faceTris.size() == cube.faceTris.size() * 4, "clonador: conserva los poligonos");
            check(std::fabs(o.boundsMax.x - 3.5f) < 1e-4f && std::fabs(o.boundsMin.x + 0.5f) < 1e-4f, "clonador: la lineal llega a x=3.5");
            cl.mode = 1; cl.count = 3; cl.countY = 2; cl.countZ = 2; cl.offset = glm::vec3(200.0f, 200.0f, 200.0f);
            check(cloneCount(cl) == 12, "clonador: cuadricula 3x2x2 = 12");
            o = cloneMeshData(cube, cl, glm::mat4(1.0f));
            check(std::fabs(o.boundsMax.x - 1.5f) < 1e-4f && std::fabs(o.boundsMin.x + 1.5f) < 1e-4f, "clonador: la cuadricula esta centrada");
            cl.mode = 2; cl.count = 6; cl.radiusCm = 400.0f; cl.axis = 1;
            o = cloneMeshData(cube, cl, glm::mat4(1.0f));
            check(o.vertices.size() == cube.vertices.size() * 6, "clonador: 6 copias radiales");
            check(std::fabs(o.boundsMax.y - 0.5f) < 1e-4f && std::fabs(o.boundsMin.y + 0.5f) < 1e-4f, "clonador: el circulo radial (eje Y) es plano en Y");
            check(o.boundsMax.x > 2.0f && o.boundsMax.z > 1.5f, "clonador: radio de 2 unidades");
            cl.mode = 0; cl.count = 3; cl.offset = glm::vec3(0.0f); cl.scaleStepPct = 100.0f;
            o = cloneMeshData(cube, cl, glm::mat4(1.0f));
            check(std::fabs(o.boundsMax.x - 1.5f) < 1e-4f, "clonador: la escala por copia crece (copia 3 = x3)");
            cl.count = 5000;
            check(cloneCount(cl) == kMaxClones, "clonador: limite de copias");
            SceneNode root("r");
            SceneNode* holder = root.addChild("Clonador", PrimitiveType::None);
            holder->clone.present = true;
            holder->clone.count = 3;
            SceneNode* kid = holder->addChild("Cubo", PrimitiveType::Mesh);
            kid->mesh = std::make_shared<const MeshData>(makeCubeMesh());
            check(evaluateCloners(root) == 1 && holder->mesh && holder->mesh->vertices.size() == cube.vertices.size() * 3, "clonador: el nodo genera la malla");
            check(evaluateCloners(root) == 0, "clonador: sin cambios no recalcula");
            holder->clone.count = 5;
            check(evaluateCloners(root) == 1 && holder->mesh->vertices.size() == cube.vertices.size() * 5, "clonador: recalcula al cambiar la cantidad");
            holder->clone.mode = 2; holder->clone.radiusCm = 123.0f; holder->clone.rotStepDeg = 7.0f; holder->clone.scaleStepPct = 12.5f;
            holder->clone.offset = glm::vec3(10.0f, 20.0f, 30.0f);
            std::vector<Material> cmats;
            const std::string text = serializeScene(root, cmats);
            SceneNode back("x");
            std::string perr;
            check(deserializeScene(text, back, cmats, &perr) && back.children().size() == 1, "clonador: serializa y lee");
            const CloneData& bc = back.children()[0]->clone;
            check(bc.present && bc.mode == 2 && bc.count == 5 && std::fabs(bc.radiusCm - 123.0f) < 1e-3f && std::fabs(bc.rotStepDeg - 7.0f) < 1e-3f &&
                      std::fabs(bc.scaleStepPct - 12.5f) < 1e-3f && std::fabs(bc.offset.z - 30.0f) < 1e-3f,
                  "clonador: ajustes conservados tras guardar");
        }

        // Booleanos: volumen con signo de la malla resultante.
        {
            auto vol = [](const MeshData& m) {
                double v = 0.0;
                for (size_t i = 0; i + 2 < m.indices.size(); i += 3) {
                    const glm::vec3 p0 = m.vertices[m.indices[i]].position, p1 = m.vertices[m.indices[i + 1]].position,
                                    p2 = m.vertices[m.indices[i + 2]].position;
                    const glm::vec3 cr = glm::cross(p1 - p0, p2 - p0);
                    const glm::vec3 nn = m.vertices[m.indices[i]].normal + m.vertices[m.indices[i + 1]].normal + m.vertices[m.indices[i + 2]].normal;
                    const double sgn = glm::dot(cr, nn) < 0.0f ? -1.0 : 1.0;  // orientacion segun las normales, como el booleano
                    v += sgn * (double)glm::dot(p0, cr) / 6.0;
                }
                return v;
            };
            MeshData ca = makeCubeMesh(), cb2 = makeCubeMesh();
            for (Vertex& v : cb2.vertices) v.position.x += 0.5f;
            MeshData u, s, in;
            std::string e;
            check(booleanMeshes(ca, cb2, BoolOp::Union, u, &e), "booleano: union de dos cubos");
            check(booleanMeshes(ca, cb2, BoolOp::Subtract, s, &e), "booleano: sustraccion de dos cubos");
            check(booleanMeshes(ca, cb2, BoolOp::Intersect, in, &e), "booleano: interseccion de dos cubos");
            check(std::fabs(vol(ca) - 1.0) < 1e-4, "booleano: el cubo unidad vale 1");
            check(std::fabs(vol(u) - 1.5) < 1e-3, "booleano: volumen de la union = 1.5");
            check(std::fabs(vol(s) - 0.5) < 1e-3, "booleano: volumen de A - B = 0.5");
            check(std::fabs(vol(in) - 0.5) < 1e-3, "booleano: volumen de la interseccion = 0.5");
            check(!u.faceTris.empty() && u.faceTris.size() < u.indices.size() / 3, "booleano: poligonos con varios triangulos");
            // Cubo y esfera: V(A u B) + V(A n B) = V(A) + V(B) y V(A - B) = V(A) - V(A n B).
            MeshData sp = makeSphereMesh();
            for (Vertex& v : sp.vertices) v.position += glm::vec3(0.3f, 0.2f, 0.1f);
            MeshData su, ss, si;
            check(booleanMeshes(ca, sp, BoolOp::Union, su, &e) && booleanMeshes(ca, sp, BoolOp::Subtract, ss, &e) &&
                      booleanMeshes(ca, sp, BoolOp::Intersect, si, &e),
                  "booleano: cubo y esfera");
            check(std::fabs(vol(su) + vol(si) - vol(ca) - vol(sp)) < 5e-3, "booleano: cubo/esfera cumple V(union)+V(inter)=V(A)+V(B)");
            check(std::fabs(vol(ss) - (vol(ca) - vol(si))) < 5e-3, "booleano: cubo/esfera cumple V(A-B)=V(A)-V(inter)");
            MeshData far = makeCubeMesh();
            for (Vertex& v : far.vertices) v.position.x += 5.0f;
            MeshData none;
            check(!booleanMeshes(ca, far, BoolOp::Intersect, none, &e) && !e.empty(), "booleano: interseccion vacia da error");
            check(!booleanMeshes(MeshData{}, ca, BoolOp::Union, none, &e), "booleano: malla vacia da error");
        }

        // Aumentar / Reducir / Conectados con la cara de arriba.
        const std::set<int> grown = growSelection(t, 3, {top});
        check(grown.size() == 5, "aumentar: arriba + 4 lados");
        check(shrinkSelection(t, 3, grown) == std::set<int>{top}, "reducir: vuelve a la de arriba");
        // Contorno y Rellenar. Cubo con la tapa subdividida: 9 poligonos (4 de arriba, 4 lados, 1 abajo).
        {
            const MeshEditResult sub = subdividePolygons(cube, t, {top});
            const MeshTopology st = buildMeshTopology(sub.mesh);
            const std::set<int> topFour = polysFromTris(st, sub.selectTris);
            int bottom = -1;
            for (int p = 0; p < (int)st.polys.size(); ++p)
                if (st.polyNormals[(size_t)p].y < -0.9f) bottom = p;
            std::set<int> ring;  // los 4 lados
            for (int p = 0; p < (int)st.polys.size(); ++p)
                if (!topFour.count(p) && p != bottom) ring.insert(p);
            check(ring.size() == 4 && bottom >= 0, "contorno/rellenar: preparacion del cubo con la tapa subdividida");
            check(fillSelection(st, ring).size() == 5, "rellenar: el hueco pequeño (abajo) se rellena y la tapa de 4 no");
            check(outlineSelection(st, 3, ring).size() == 4, "contorno: los 4 lados tocan algo sin seleccionar");
            std::set<int> big = ring;
            big.insert(bottom);
            check(outlineSelection(st, 3, big).size() == 4 && !outlineSelection(st, 3, big).count(bottom),
                  "contorno: con la base tambien, la base queda dentro y sale solo el borde");
            check(outlineSelection(st, 3, {bottom}).size() == 1, "contorno: un poligono es todo borde");
        }
        check(connectedSelection(t, 3, {top}).size() == 6, "conectados: el cubo entero");
        check(growSelection(t, 1, {0}).size() == 4, "aumentar puntos: el punto y sus 3 vecinos");

        // Soldar los 4 puntos de arriba: piramide de 5 puntos y 5 poligonos.
        std::set<int> topPts(t.polys[(size_t)top].begin(), t.polys[(size_t)top].end());
        const MeshTopology wt = buildMeshTopology(weldPoints(cube, t, topPts).mesh);
        check(wt.points.size() == 5 && wt.polys.size() == 5 && euler(wt) == 2, "soldar: piramide 5/8/5");

        // Cerrar Agujero: el cubo sin la cara de arriba vuelve a cerrarse.
        std::vector<char> keep(t.polys.size(), 1);
        keep[(size_t)top] = 0;
        const MeshData open = keepPolygons(cube, t, keep);
        const MeshTopology ot = buildMeshTopology(open);
        const MeshEditResult ch = closeHoles(open, ot, {});
        const MeshTopology ct = buildMeshTopology(ch.mesh);
        check(ch.changed == 1 && ct.polys.size() == 6 && euler(ct) == 2, "cerrar agujero: cubo cerrado de nuevo");
        check(polysFromTris(ct, ch.selectTris).size() == 1 && ct.polyNormals[(size_t)*polysFromTris(ct, ch.selectTris).begin()].y > 0.9f,
              "cerrar agujero: la tapa nueva mira hacia arriba");
        check(closeHoles(open, ot, {999999}).changed == 0, "cerrar agujero: solo los bordes que tocan la seleccion");

        // Comprobar Malla: el cubo cerrado esta limpio; sin la cara de arriba tiene 4 aristas abiertas.
        check(checkMesh(t).clean(), "comprobar malla: el cubo esta limpio");
        check(checkMesh(ot).openEdges == 4 && !checkMesh(ot).clean(), "comprobar malla: 4 aristas abiertas sin la tapa");

        // Puente: dos cubos de una cara (arriba del de abajo, abajo del de arriba) -> el anillo vuelve a unirlos.
        {
            // Dos cuadrados paralelos: A (y=0, mira abajo) y B (y=1, mira arriba) del mismo cubo sin laterales.
            std::vector<char> keepCaps(t.polys.size(), 0);
            int bot = -1;
            for (int p = 0; p < (int)t.polys.size(); ++p)
                if (t.polyNormals[(size_t)p].y < -0.9f) bot = p;
            keepCaps[(size_t)top] = 1;
            keepCaps[(size_t)bot] = 1;
            const MeshData caps = keepPolygons(cube, t, keepCaps);
            const MeshTopology capt = buildMeshTopology(caps);
            const MeshEditResult br = bridgePolygons(caps, capt, 0, 1);
            const MeshTopology brt = buildMeshTopology(br.mesh);
            check(br.changed == 4 && brt.polys.size() == 4 && brt.points.size() == 8, "puente: 4 cuadrilateros entre las dos tapas");
            check(bridgePolygons(caps, capt, 0, 0).changed == 0, "puente: no con el mismo poligono");
            // El tubo no tiene tapas: 8 aristas abiertas, ninguna no manifold.
            const MeshCheck tc = checkMesh(brt);
            check(tc.openEdges == 8 && tc.nonManifoldEdges == 0, "puente: tubo abierto por arriba y abajo");
        }

        // Subdividir la cara de arriba: 4 cuadrilateros, vecinos con punto medio.
        const MeshEditResult sd = subdividePolygons(cube, t, {top});
        const MeshTopology sdt = buildMeshTopology(sd.mesh);
        check(sdt.points.size() == 13 && sdt.polys.size() == 9 && euler(sdt) == 2, "subdividir arriba: 13 puntos, 9 poligonos, cerrada");
        check(polysFromTris(sdt, sd.selectTris).size() == 4, "subdividir: quedan seleccionados los 4 nuevos");
        const MeshTopology sall = buildMeshTopology(subdividePolygons(cube, t, {}).mesh);
        check(sall.points.size() == 26 && sall.polys.size() == 24 && euler(sall) == 2, "subdividir todo: 26 puntos, 24 poligonos");

        // Invertir Normales de la cara de arriba.
        const MeshTopology ft = buildMeshTopology(flipPolygons(cube, t, {top}).mesh);
        bool down = false;
        for (const glm::vec3& n : ft.polyNormals) down = down || n.y < -0.9f;
        int downs = 0;
        for (const glm::vec3& n : ft.polyNormals) downs += n.y < -0.9f ? 1 : 0;
        check(down && downs == 2, "invertir normales: la de arriba mira ahora hacia abajo");

        // Triangular: 6 cuadrilateros -> 12 triangulos, mismos 8 puntos, sigue cerrada.
        const MeshEditResult tr = triangulatePolygons(cube, t, {});
        const MeshTopology trt = buildMeshTopology(tr.mesh);
        check(tr.changed == 6 && trt.points.size() == 8 && euler(trt) == 2, "triangular: 6 cuadrilateros pasan a triangulos, 8 puntos");
        check(triangulatePolygons(tr.mesh, trt, {}).changed == 0, "triangular: lo ya triangulado no cambia");

        // Destriangular: los 12 triangulos del cubo vuelven a 6 cuadrilateros; sin triangulos, no cambia.
        const MeshEditResult ut = untriangulatePolygons(tr.mesh, trt, {});
        const MeshTopology utt = buildMeshTopology(ut.mesh);
        check(ut.changed == 6 && utt.polys.size() == 6 && utt.points.size() == 8 && euler(utt) == 2,
              "destriangular: 12 triangulos del cubo -> 6 cuadrilateros");
        check(untriangulatePolygons(cube, t, {}).changed == 0, "destriangular: sin triangulos no cambia");

        // Alinear Normales: con una cara al reves se arregla; una malla ya coherente no cambia.
        std::set<int> all;
        for (int p = 0; p < (int)t.polys.size(); ++p) all.insert(p);
        check(alignNormals(cube, t, all).changed == 0, "alinear normales: el cubo coherente no cambia");
        const MeshData badMesh = flipPolygons(cube, t, {top}).mesh;
        const MeshTopology badT = buildMeshTopology(badMesh);
        const MeshEditResult al = alignNormals(badMesh, badT, all);
        check(al.changed > 0, "alinear normales: detecta la cara invertida");
        const MeshTopology alT = buildMeshTopology(al.mesh);
        check(alignNormals(al.mesh, alT, all).changed == 0, "alinear normales: despues queda coherente");
    }

    // --- Ida y vuelta exacta por .iron (formato 7) --------------------------------
    {
        SceneNode root("Raiz");
        std::vector<Material> mats;
        testio::buildScene(root, mats, "C:/texturas de prueba");
        const std::string text = serializeScene(root, mats);
        SceneNode back("Raiz");
        std::vector<Material> mats2;
        std::string err;
        check(deserializeScene(text, back, mats2, &err), "iron v7: se vuelve a leer");
        const testio::Diffs d = testio::compareScenes(root, mats, back, mats2, testio::CompareOptions{});
        for (const std::string& s : d.list) std::printf("  iron: %s\n", s.c_str());
        check(d.list.empty(), "iron v7: todo vuelve exacto (luces, camaras, canales, capas, UV, HPB, claves)");
        check(serializeScene(back, mats2) == text, "iron v7: guardar lo leido da el mismo texto");
        check(mats2.size() == 3 && mats2[0].texBaseDir == "C:/texturas de prueba", "iron v7: carpeta de texturas");
        // Un .iron de version 6 (sin escapes) sigue leyendo "%" tal cual.
        SceneNode old("Raiz");
        std::vector<Material> om;
        check(deserializeScene("IRON3D_SCENE 6\nMATERIALS {\n}\nNODE name=\"Raiz\" primitive=none {\n  NODE "
                               "name=\"50%22\" primitive=none {\n  }\n}\n",
                               old, om, &err) &&
                  old.children().size() == 1 && old.children()[0]->name() == "50%22",
              "iron v6: los nombres con % no se desescapan");
    }

    // --- UV: Iron3D -> C4D -> Iron3D conserva cada esquina -------------------------
    {
        const MeshData src = testio::uvMesh();
        const c4d::PolySoup soup = c4d::fromIronMesh(src);
        check(soup.uvs.size() == soup.polys.size(), "uv: una UV por esquina de cada poligono");
        check(soup.polys.size() == 2, "uv: el cuadrado sale como un cuadrilatero y el triangulo aparte");
        const MeshData back = c4d::toIronMesh(soup, -1.0);
        check(testio::corners(src, 1e-5f) == testio::corners(back, 1e-5f), "uv: mismas esquinas (posicion + UV)");
        // Costura: mismas posiciones, UV distintas en la arista comun -> no se juntan.
        MeshData seam = src;
        seam.vertices[2].uv = glm::vec2(0.9f, 0.9f); // solo en el primer triangulo
        seam.vertices.push_back(src.vertices[2]);
        seam.indices[4] = (uint32_t)seam.vertices.size() - 1;
        check(c4d::fromIronMesh(seam).polys.size() == 3, "uv: una costura no se junta en cuadrilatero");
        // Anadir a una sopa vacia (lo que hace el importador) conserva las UV.
        c4d::PolySoup joined;
        joined.append(soup, glm::dmat4(1.0));
        check(joined.uvs.size() == joined.polys.size() && joined.uvs == soup.uvs, "uv: append a una sopa vacia las conserva");
        c4d::PolySoup mixed = c4d::makeCube({1.0, 1.0, 1.0});
        mixed.append(soup, glm::dmat4(1.0));
        check(mixed.uvs.size() == mixed.polys.size() && mixed.uvs.back() == soup.uvs.back(),
              "uv: append a una sopa sin UV rellena con ceros y conserva las nuevas");
    }

    // --- Capas de Reflectancia <-> PBR ---------------------------------------------
    {
        Material m;
        m.metallic = 1.0f;
        m.roughness = 0.3f;
        m.baseColor = {0.9f, 0.6f, 0.2f};
        const ReflectLayer l = layerFromPbr(m);
        glm::vec3 c(0.0f);
        float metal = 0.0f, rough = 0.0f;
        check(pbrFromLayers({l}, true, c, metal, rough) && metal == 1.0f && rough == 0.3f && c == m.baseColor,
              "pbr: la capa generada vuelve a dar los mismos valores (metal)");
        m.layers = {l};
        check(layersMatchPbr(m), "pbr: capas sin tocar se reconocen");
        m.roughness = 0.31f;
        check(!layersMatchPbr(m), "pbr: si se edita la rugosidad, las capas ya no valen");
        Material d;
        d.roughness = 0.45f;
        d.baseColor = {0.1f, 0.2f, 0.3f};
        glm::vec3 dc = d.baseColor;
        check(pbrFromLayers({layerFromPbr(d)}, true, dc, metal, rough) && metal == 0.0f && rough == 0.45f &&
                  dc == d.baseColor,
              "pbr: la capa generada vuelve a dar los mismos valores (dielectrico)");
        CameraData cam;
        check(std::fabs(cam.fovDeg() - 53.130102f) < 1e-3f, "camara: campo de vision de 36 mm con sensor de 36 mm");
    }

    if (g_failures == 0) std::printf("c4d_geometry: todo correcto\n");
    return g_failures == 0 ? 0 : 1;
}
