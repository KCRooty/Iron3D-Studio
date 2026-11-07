// Pruebas de la geometria compatible con C4D (importers/c4d_compat). Sin GPU
// ni Cineware: se ejecutan en el CI de Windows y Linux.

#include "core/SceneNode.h"
#include "core/MeshTopology.h"
#include "core/SceneSerializer.h"
#include "core/ThumbnailRenderer.h"
#include "importers/c4d_compat/C4DGeometry.h"

#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <functional>

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

    if (g_failures == 0) std::printf("c4d_geometry: todo correcto\n");
    return g_failures == 0 ? 0 : 1;
}
