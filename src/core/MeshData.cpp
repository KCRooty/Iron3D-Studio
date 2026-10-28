#include "MeshData.h"

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <cmath>

namespace iron3d {

// Cubo con normales de cara planas (Fase 5): 24 vertices (4 por cara) en vez
// de los 8 de esquina compartida de antes -- una esquina de cubo ve 3 caras
// con normales distintas, asi que no se puede iluminar bien compartiendo
// vertices entre caras. Mismo "tamano 1 unidad" y mismo color por cara que
// antes (ya no se ve directamente con iluminacion real, pero se deja como
// tinte visible en el pipeline sin luz / por si se quiere comparar).
MeshData makeCubeMesh() {
    MeshData mesh;
    const float h = 0.5f;

    struct Face { glm::vec3 normal; glm::vec3 corners[4]; glm::vec3 color; };
    const Face faces[6] = {
        {{ 0, 0, -1}, {{-h,-h,-h}, { h,-h,-h}, { h, h,-h}, {-h, h,-h}}, {0.9f, 0.2f, 0.2f}}, // -Z trasera
        {{ 0, 0,  1}, {{-h,-h, h}, { h,-h, h}, { h, h, h}, {-h, h, h}}, {0.2f, 0.9f, 0.9f}}, // +Z frontal
        {{-1, 0,  0}, {{-h,-h, h}, {-h,-h,-h}, {-h, h,-h}, {-h, h, h}}, {0.9f, 0.2f, 0.9f}}, // -X izquierda
        {{ 1, 0,  0}, {{ h,-h,-h}, { h,-h, h}, { h, h, h}, { h, h,-h}}, {0.2f, 0.9f, 0.2f}}, // +X derecha
        {{ 0, 1,  0}, {{-h, h,-h}, { h, h,-h}, { h, h, h}, {-h, h, h}}, {0.95f, 0.95f, 0.95f}}, // +Y arriba
        {{ 0,-1,  0}, {{-h,-h, h}, { h,-h, h}, { h,-h,-h}, {-h,-h,-h}}, {0.15f, 0.15f, 0.15f}}, // -Y abajo
    };

    for (const Face& f : faces) {
        const uint16_t base = (uint16_t)mesh.vertices.size();
        for (const glm::vec3& corner : f.corners) {
            mesh.vertices.push_back({corner, f.color, f.normal});
        }
        // CCW visto desde fuera (coherente con el orden de esquinas de cada cara).
        mesh.indices.insert(mesh.indices.end(), {
            (uint16_t)(base + 0), (uint16_t)(base + 1), (uint16_t)(base + 2),
            (uint16_t)(base + 2), (uint16_t)(base + 3), (uint16_t)(base + 0),
        });
    }
    return mesh;
}

// Esfera UV de radio 0.5: la normal es la propia direccion desde el centro
// (una esfera centrada en el origen es el caso facil, normal == posicion
// normalizada). El color ya no representa la normal -- antes era la unica
// forma de "ver" la direccion sin iluminacion real; ahora que la hay, se deja
// un tinte neutro, el aspecto final lo da el material + la luz.
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
            mesh.vertices.push_back({dir * radius, glm::vec3(0.8f), dir});
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

// Plano 1x1 en el eje XZ (el plano "de suelo" por defecto de C4D), normal
// hacia +Y -- visible desde ambas caras porque el pipeline que lo dibuja
// tiene cullMode=NONE, asi que de momento no importa que una cara "trasera"
// tenga la normal invertida de cara a la camara (documentado, suficiente
// para un primitivo de referencia en esta fase).
MeshData makePlaneMesh() {
    MeshData mesh;
    const glm::vec3 up(0.0f, 1.0f, 0.0f);
    mesh.vertices = {
        {{-0.5f, 0.0f, -0.5f}, {0.3f, 0.5f, 0.3f}, up},
        {{ 0.5f, 0.0f, -0.5f}, {0.3f, 0.6f, 0.4f}, up},
        {{ 0.5f, 0.0f,  0.5f}, {0.4f, 0.6f, 0.3f}, up},
        {{-0.5f, 0.0f,  0.5f}, {0.3f, 0.55f, 0.35f}, up},
    };
    mesh.indices = {0, 2, 1, 2, 0, 3};
    return mesh;
}

} // namespace iron3d
