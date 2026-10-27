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
    glm::mat4 projectionMatrix(float aspectRatio) const;

private:
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
