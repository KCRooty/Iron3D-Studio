#include "MeshData.h"

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <cmath>

namespace iron3d {

MeshData makeCubeMesh() {
    MeshData mesh;
    // Mismos 8 vertices/12 triangulos del cubo original de la Fase 1 (un
    // color por esquina, CCW visto desde fuera para VK_CULL_MODE_BACK_BIT).
    mesh.vertices = {
        {{-0.5f, -0.5f, -0.5f}, {0.9f, 0.2f, 0.2f}},
        {{ 0.5f, -0.5f, -0.5f}, {0.2f, 0.9f, 0.2f}},
        {{ 0.5f,  0.5f, -0.5f}, {0.2f, 0.2f, 0.9f}},
        {{-0.5f,  0.5f, -0.5f}, {0.9f, 0.9f, 0.2f}},
        {{-0.5f, -0.5f,  0.5f}, {0.9f, 0.2f, 0.9f}},
        {{ 0.5f, -0.5f,  0.5f}, {0.2f, 0.9f, 0.9f}},
        {{ 0.5f,  0.5f,  0.5f}, {0.95f, 0.95f, 0.95f}},
        {{-0.5f,  0.5f,  0.5f}, {0.15f, 0.15f, 0.15f}},
    };
    mesh.indices = {
        0, 1, 2,  2, 3, 0,   // trasera  (-Z)
        5, 4, 7,  7, 6, 5,   // frontal  (+Z)
        4, 0, 3,  3, 7, 4,   // izquierda (-X)
        1, 5, 6,  6, 2, 1,   // derecha   (+X)
        3, 2, 6,  6, 7, 3,   // arriba    (+Y)
        4, 5, 1,  1, 0, 4,   // abajo     (-Y)
    };
    return mesh;
}

// Esfera UV de radio 0.5 (mismo "tamano por defecto 1 unidad" que el cubo).
// Color = direccion desde el centro mapeada a RGB -- visualizacion de
// normales clasica, da el mismo aspecto arcoiris que el cubo sin necesitar
// iluminacion real todavia.
MeshData makeSphereMesh(int rings, int segments) {
    MeshData mesh;
    const float radius = 0.5f;

    for (int ring = 0; ring <= rings; ++ring) {
        const float v = (float)ring / (float)rings;          // 0 (polo sur) .. 1 (polo norte)
        const float phi = v * glm::pi<float>();
        const float y = std::cos(phi);
        const float ringRadius = std::sin(phi);

        for (int seg = 0; seg <= segments; ++seg) {
            const float u = (float)seg / (float)segments;
            const float theta = u * glm::two_pi<float>();
            const glm::vec3 dir(ringRadius * std::cos(theta), y, ringRadius * std::sin(theta));
            const glm::vec3 color = dir * 0.5f + glm::vec3(0.5f);
            mesh.vertices.push_back({dir * radius, color});
        }
    }

    const int stride = segments + 1;
    for (int ring = 0; ring < rings; ++ring) {
        for (int seg = 0; seg < segments; ++seg) {
            const uint16_t a = (uint16_t)(ring * stride + seg);
            const uint16_t b = (uint16_t)(ring * stride + seg + 1);
            const uint16_t c = (uint16_t)((ring + 1) * stride + seg);
            const uint16_t d = (uint16_t)((ring + 1) * stride + seg + 1);
            // CCW visto desde fuera, igual que el cubo.
            mesh.indices.insert(mesh.indices.end(), {a, c, b, b, c, d});
        }
    }
    return mesh;
}

// Plano 1x1 en el eje XZ (el plano "de suelo" por defecto de C4D), visible
// desde ambas caras porque el gizmo/objetos lo usan como referencia de
// escena -- sin cull (el pipeline que lo dibuja ya tiene cullMode=BACK,
// asi que de momento solo se ve desde +Y; documentado, suficiente para un
// primitivo de referencia en esta fase).
MeshData makePlaneMesh() {
    MeshData mesh;
    mesh.vertices = {
        {{-0.5f, 0.0f, -0.5f}, {0.3f, 0.5f, 0.3f}},
        {{ 0.5f, 0.0f, -0.5f}, {0.3f, 0.6f, 0.4f}},
        {{ 0.5f, 0.0f,  0.5f}, {0.4f, 0.6f, 0.3f}},
        {{-0.5f, 0.0f,  0.5f}, {0.3f, 0.55f, 0.35f}},
    };
    mesh.indices = {0, 2, 1, 2, 0, 3};
    return mesh;
}

} // namespace iron3d
