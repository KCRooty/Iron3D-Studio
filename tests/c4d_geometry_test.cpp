// Pruebas de la geometria compatible con C4D (importers/c4d_compat). Sin GPU
// ni Cineware: se ejecutan en el CI de Windows y Linux.

#include "core/SceneNode.h"
#include "core/MeshEdit.h"
#include "core/MeshTopology.h"
#include "core/SceneSerializer.h"
#include "core/ThumbnailRenderer.h"
#include "core/TransformGizmo.h"
#include "importers/c4d_compat/C4DGeometry.h"

#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <cstdlib>
#include <cstdio>
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

        // Aumentar / Reducir / Conectados con la cara de arriba.
        const std::set<int> grown = growSelection(t, 3, {top});
        check(grown.size() == 5, "aumentar: arriba + 4 lados");
        check(shrinkSelection(t, 3, grown) == std::set<int>{top}, "reducir: vuelve a la de arriba");
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
    }

    if (g_failures == 0) std::printf("c4d_geometry: todo correcto\n");
    return g_failures == 0 ? 0 : 1;
}
