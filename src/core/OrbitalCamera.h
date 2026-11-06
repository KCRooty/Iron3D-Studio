#pragma once

// Navegacion del visor "Alt + clics" (Documento Maestro §2.7, replicado 1:1 de
// C4D): Alt + boton izquierdo = orbitar, Alt + boton medio = paneo,
// Alt + boton derecho (o rueda) = zoom/dolly. Coordenadas esfericas alrededor
// de un punto objetivo -- igual que la camara del visor de C4D.

#include <glm/glm.hpp>

struct GLFWwindow;

namespace iron3d {

class OrbitalCamera {
public:
    void handleInput(GLFWwindow* window, double mouseX, double mouseY, double scrollY);
    void resetMouseDelta(double mouseX, double mouseY);

    glm::mat4 viewMatrix() const;
    // `flipY`: Vulkan tiene +Y hacia abajo en espacio de recorte (al reves
    // que OpenGL) -- GLM genera la matriz para la convencion de GL, asi que
    // Vulkan necesita invertir el signo. Por defecto true (el unico backend
    // hasta la Fase 3b); IRenderer::needsProjectionYFlip() dice que pasar
    // segun el backend activo.
    glm::mat4 projectionMatrix(float aspectRatio, bool flipY = true) const;
    glm::vec3 eyePosition() const;

    // Mismas operaciones que los atajos Alt+clic, en pixeles de raton --
    // tambien las usan los iconos de navegacion de la esquina del visor.
    void orbit(float dxPixels, float dyPixels);
    void pan(float dxPixels, float dyPixels);
    void dolly(float dyPixels);
    void resetView();
    // Encuadrar (tecla H / "Encuadrar Geometria" en C4D): centra el objetivo
    // en `center` a una distancia que deja ver una esfera de radio `radius`.
    void frame(const glm::vec3& center, float radius);
    // Distancia a la que frame() dejaria ver una esfera de radio `radius`.
    static float frameDistance(float radius);
    // Objetivo y distancia actuales: la flecha de "objeto fuera de vista" los
    // interpola para que la camara viaje en vez de saltar.
    glm::vec3 target() const { return target_; }
    float distance() const { return distance_; }
    void setTargetDistance(const glm::vec3& target, float distance);

    // Vistas de C4D (F1-F4): Perspectiva y las ortograficas Superior, Derecha
    // y Frontal; Izquierda, Trasera e Inferior desde el menu Camaras. En las
    // ortograficas no se orbita (como en C4D): Alt+clic izquierdo no hace
    // nada, el paneo y el zoom si.
    enum class View { Perspective, Top, Right, Front, Left, Back, Bottom };
    void setView(View v);
    View view() const { return view_; }
    bool isOrtho() const { return view_ != View::Perspective; }

private:
    View view_ = View::Perspective;
    glm::vec3 target_{0.0f, 0.0f, 0.0f};
    float distance_ = 4.0f;
    float yaw_ = glm::radians(45.0f);
    float pitch_ = glm::radians(30.0f);

    double lastMouseX_ = 0.0;
    double lastMouseY_ = 0.0;
    bool hasLastMouse_ = false;

    static constexpr float kOrbitSpeed = 0.01f;
    static constexpr float kPanSpeed = 0.0025f;
    static constexpr float kZoomSpeed = 0.5f;
    static constexpr float kMinDistance = 0.5f;
    static constexpr float kMaxDistance = 50.0f;
    static constexpr float kMaxPitch = glm::radians(89.0f);
};

} // namespace iron3d
