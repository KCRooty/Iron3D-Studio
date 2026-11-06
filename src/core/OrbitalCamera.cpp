#include "OrbitalCamera.h"

#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>

namespace iron3d {

void OrbitalCamera::resetMouseDelta(double mouseX, double mouseY) {
    lastMouseX_ = mouseX;
    lastMouseY_ = mouseY;
    hasLastMouse_ = true;
}

void OrbitalCamera::handleInput(GLFWwindow* window, double mouseX, double mouseY, double scrollY) {
    if (!hasLastMouse_) {
        resetMouseDelta(mouseX, mouseY);
    }
    const double dx = mouseX - lastMouseX_;
    const double dy = mouseY - lastMouseY_;
    lastMouseX_ = mouseX;
    lastMouseY_ = mouseY;

    const bool alt = glfwGetKey(window, GLFW_KEY_LEFT_ALT) == GLFW_PRESS ||
                      glfwGetKey(window, GLFW_KEY_RIGHT_ALT) == GLFW_PRESS;

    if (alt && glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS) {
        orbit((float)dx, (float)dy);
    } else if (alt && glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_MIDDLE) == GLFW_PRESS) {
        pan((float)dx, (float)dy);
    } else if (alt && glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS) {
        dolly((float)dy);
    }

    if (scrollY != 0.0) {
        distance_ = std::clamp(distance_ - (float)scrollY * kZoomSpeed, kMinDistance, kMaxDistance);
    }
}

void OrbitalCamera::setView(View v) {
    view_ = v;
    switch (v) {
        case View::Top: yaw_ = 0.0f; pitch_ = glm::radians(90.0f); break;   // desde +Y
        case View::Right: yaw_ = glm::radians(90.0f); pitch_ = 0.0f; break; // desde +X
        case View::Front: yaw_ = 0.0f; pitch_ = 0.0f; break;                // desde +Z
        case View::Perspective: yaw_ = glm::radians(45.0f); pitch_ = glm::radians(30.0f); break;
    }
}

void OrbitalCamera::orbit(float dxPixels, float dyPixels) {
    if (isOrtho()) return;  // en C4D las vistas ortograficas no orbitan
    // Como en C4D, la escena "sigue" al raton (como si la agarraras): arrastrar
    // a la derecha gira el objeto hacia la derecha, es decir, la camara se
    // va hacia la izquierda; arrastrar hacia abajo la sube. Antes era al
    // reves (la camara seguia al raton) y Core lo noto invertido (29 oct).
    yaw_ -= dxPixels * kOrbitSpeed;
    pitch_ = std::clamp(pitch_ + dyPixels * kOrbitSpeed, -kMaxPitch, kMaxPitch);
}

void OrbitalCamera::pan(float dxPixels, float dyPixels) {
    // Ejes de la camara sacados de la vista (filas de la rotacion): valen
    // tambien mirando en vertical (vista Superior), donde cross(forward, Y) es 0.
    const glm::mat4 v = viewMatrix();
    const glm::vec3 right(v[0][0], v[1][0], v[2][0]);
    const glm::vec3 up(v[0][1], v[1][1], v[2][1]);
    const float scale = distance_ * kPanSpeed;
    target_ -= right * dxPixels * scale;
    target_ += up * dyPixels * scale;
}

void OrbitalCamera::dolly(float dyPixels) {
    distance_ = std::clamp(distance_ + dyPixels * kZoomSpeed * 0.05f, kMinDistance, kMaxDistance);
}

void OrbitalCamera::resetView() {
    target_ = glm::vec3(0.0f);
    distance_ = 4.0f;
    yaw_ = glm::radians(45.0f);
    pitch_ = glm::radians(30.0f);
    if (isOrtho()) setView(view_);  // una vista ortografica conserva su direccion
}

float OrbitalCamera::frameDistance(float radius) {
    // FOV vertical de 50 grados (ver projectionMatrix): margen de ~20%.
    return std::clamp(radius * 1.2f / std::sin(glm::radians(25.0f)), kMinDistance, kMaxDistance);
}

void OrbitalCamera::frame(const glm::vec3& center, float radius) {
    target_ = center;
    distance_ = frameDistance(radius);
}

void OrbitalCamera::setTargetDistance(const glm::vec3& target, float distance) {
    target_ = target;
    distance_ = std::clamp(distance, kMinDistance, kMaxDistance);
}

glm::vec3 OrbitalCamera::eyePosition() const {
    return target_ + glm::vec3(
        distance_ * std::cos(pitch_) * std::sin(yaw_),
        distance_ * std::sin(pitch_),
        distance_ * std::cos(pitch_) * std::cos(yaw_));
}

glm::mat4 OrbitalCamera::viewMatrix() const {
    // Mirando en vertical, "arriba" en pantalla es -Z (como la vista Superior de C4D).
    const glm::vec3 up = view_ == View::Top ? glm::vec3(0.0f, 0.0f, -1.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
    return glm::lookAt(eyePosition(), target_, up);
}

glm::mat4 OrbitalCamera::projectionMatrix(float aspectRatio, bool flipY) const {
    glm::mat4 proj;
    if (isOrtho()) {
        // Mismo encuadre que la perspectiva a la distancia del objetivo; el
        // volumen se extiende detras del ojo para no recortar nada.
        const float halfH = distance_ * std::tan(glm::radians(25.0f));
        const float halfW = halfH * aspectRatio;
        proj = glm::ortho(-halfW, halfW, -halfH, halfH, -100.0f, 100.0f);
    } else {
        proj = glm::perspective(glm::radians(50.0f), aspectRatio, 0.05f, 100.0f);
    }
    // Vulkan: +Y apunta hacia abajo en el espacio de recorte (al reves que GL).
    if (flipY) proj[1][1] *= -1.0f;
    return proj;
}

} // namespace iron3d
