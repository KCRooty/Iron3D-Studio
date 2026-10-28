#include "IRenderer.h"
#include "VulkanRenderer.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <cstdio>
#include <cstdlib>

namespace iron3d {

void configureWindowHints(RendererBackend backend) {
    if (backend == RendererBackend::Vulkan) {
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        return;
    }
    // OpenGL (Fase 3b): perfil core 3.3, el minimo que cubre todo lo que
    // necesita este proyecto (VAOs, buffers con layout explicito, sin nada
    // del pipeline fijo antiguo).
    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_API);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#if defined(__APPLE__)
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#endif
}

std::unique_ptr<IRenderer> createRenderer(RendererBackend backend) {
    if (backend == RendererBackend::Vulkan) {
        return std::make_unique<VulkanRenderer>();
    }
    // Fase 3b, todavia no implementado en este commit.
    std::fprintf(stderr, "[Iron3D] Backend OpenGL: pendiente (Fase 3b).\n");
    std::abort();
}

} // namespace iron3d
