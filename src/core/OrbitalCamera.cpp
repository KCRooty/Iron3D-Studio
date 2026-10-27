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
        // Orbitar.
        yaw_ += (float)dx * kOrbitSpeed;
        pitch_ = std::clamp(pitch_ - (float)dy * kOrbitSpeed, -kMaxPitch, kMaxPitch);
    } else if (alt && glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_MIDDLE) == GLFW_PRESS) {
        // Panear en el plano de la camara.
        const glm::vec3 forward = glm::normalize(target_ - (target_ + glm::vec3(
            distance_ * std::cos(pitch_) * std::sin(yaw_),
            distance_ * std::sin(pitch_),
            distance_ * std::cos(pitch_) * std::cos(yaw_))));
        const glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3(0.0f, 1.0f, 0.0f)));
        const glm::vec3 up = glm::normalize(glm::cross(right, forward));
        const float scale = distance_ * kPanSpeed;
        target_ -= right * (float)dx * scale;
        target_ += up * (float)dy * scale;
    } else if (alt && glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS) {
        // Zoom arrastrando (ademas de la rueda, mas abajo).
        distance_ = std::clamp(distance_ + (float)dy * kZoomSpeed * 0.05f, kMinDistance, kMaxDistance);
    }

    if (scrollY != 0.0) {
        distance_ = std::clamp(distance_ - (float)scrollY * kZoomSpeed, kMinDistance, kMaxDistance);
    }
}

glm::mat4 OrbitalCamera::viewMatrix() const {
    const glm::vec3 eye = target_ + glm::vec3(
        distance_ * std::cos(pitch_) * std::sin(yaw_),
        distance_ * std::sin(pitch_),
        distance_ * std::cos(pitch_) * std::cos(yaw_));
    return glm::lookAt(eye, target_, glm::vec3(0.0f, 1.0f, 0.0f));
}

glm::mat4 OrbitalCamera::projectionMatrix(float aspectRatio) const {
    glm::mat4 proj = glm::perspective(glm::radians(50.0f), aspectRatio, 0.05f, 100.0f);
    // Vulkan: +Y apunta hacia abajo en el espacio de recorte (al reves que GL).
    proj[1][1] *= -1.0f;
    return proj;
}

} // namespace iron3d
