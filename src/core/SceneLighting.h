#pragma once

// Luces de la escena tal como las usa el visor (2026-11-10). Antes el visor
// iluminaba con UNA luz puntual blanca puesta donde estuviera la primera Luz:
// daba igual el color, la intensidad, el tipo o cuantas hubiera. Esto recoge
// todas las Luces visibles, en mundo, con lo que el shader necesita de cada
// una. Comun a los dos backends (y al render por CPU cuando lo use).
//
// Como en el visor de C4D: si la escena no tiene ninguna luz, se usa la luz
// por defecto (una luz desde la camara); en cuanto hay una, solo cuentan las
// de la escena.

#include <glm/glm.hpp>
#include <vector>

namespace iron3d {

class SceneNode;

constexpr int kMaxViewportLights = 32;

struct ViewportLight {
    enum class Type { Point = 0, Spot = 1, Directional = 2 };
    Type type = Type::Point;
    glm::vec3 position{0.0f};
    glm::vec3 direction{0.0f, 0.0f, -1.0f}; // hacia donde emite (eje Z de la luz en C4D)
    glm::vec3 color{1.0f};                  // color * intensidad
    // Foco: cosenos de la mitad de los angulos exterior e interior.
    float cosOuter = -1.0f;
    float cosInner = -1.0f;
    int falloff = 0;          // LIGHT_DETAILS_FALLOFF de C4D (ver falloffFactor)
    float radius = 1.0f;      // Radio de decaimiento, en unidades
    float innerRadius = 0.0f; // Radio interior
    bool castsShadow = false;
    bool diffuse = true;  // General > Difusion
    bool specular = true; // General > Especular
    // Indicadores para los shaders (cone.w): 1 = difusion, 2 = especular.
    int shaderFlags() const { return (diffuse ? 1 : 0) | (specular ? 2 : 0); }
};

struct SceneLighting {
    std::vector<ViewportLight> lights; // vacio = luz por defecto
    int shadowLight = -1;              // indice de la luz que hace el mapa de sombras, o -1
};

// Recorre la escena con la visibilidad de editor heredada y devuelve hasta
// kMaxViewportLights luces que aporten luz (intensidad y color no nulos).
SceneLighting collectSceneLights(const SceneNode& root);

// Atenuacion por distancia de C4D, igual que en lit.frag y en el shader de
// OpenGL: 0 ninguna, 5 escalon, 6/9 inversa (con/sin tope), 7/10 cuadrado
// inverso (con/sin tope), 8 lineal. `radius` es la distancia a la que la luz
// da el 100 % en las inversas, y donde llega a 0 en la lineal y el escalon.
float falloffFactor(int falloff, float distance, float radius, float innerRadius);

} // namespace iron3d
