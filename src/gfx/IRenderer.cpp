#include "IRenderer.h"
#include "VulkanRenderer.h"
#include "OpenGLRenderer.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <vulkan/vulkan.h>

#include <vector>

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

bool vulkanUsable() {
    // glfwVulkanSupported carga vulkan-1.dll por su cuenta: si no esta (sin
    // drivers Vulkan), no se llega a tocar ninguna funcion vk* (el .exe la
    // carga en diferido, /DELAYLOAD).
    if (!glfwVulkanSupported()) return false;
    uint32_t extCount = 0;
    const char** ext = glfwGetRequiredInstanceExtensions(&extCount);
    if (!ext) return false;
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "Iron3D Studio";
    app.apiVersion = VK_API_VERSION_1_3;
    VkInstanceCreateInfo ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ci.pApplicationInfo = &app;
    ci.enabledExtensionCount = extCount;
    ci.ppEnabledExtensionNames = ext;
    VkInstance inst = VK_NULL_HANDLE;
    if (vkCreateInstance(&ci, nullptr, &inst) != VK_SUCCESS) return false;
    bool ok = false;
    uint32_t n = 0;
    if (vkEnumeratePhysicalDevices(inst, &n, nullptr) == VK_SUCCESS && n > 0) {
        std::vector<VkPhysicalDevice> gpus(n);
        vkEnumeratePhysicalDevices(inst, &n, gpus.data());
        // La misma eleccion que VulkanContext: discreta si hay, si no la primera.
        VkPhysicalDevice gpu = gpus[0];
        for (VkPhysicalDevice g : gpus) {
            VkPhysicalDeviceProperties props{};
            vkGetPhysicalDeviceProperties(g, &props);
            if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
                gpu = g;
                break;
            }
        }
        uint32_t qn = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(gpu, &qn, nullptr);
        std::vector<VkQueueFamilyProperties> qs(qn);
        vkGetPhysicalDeviceQueueFamilyProperties(gpu, &qn, qs.data());
        for (const VkQueueFamilyProperties& q : qs) ok = ok || (q.queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0;
    }
    vkDestroyInstance(inst, nullptr);
    return ok;
}

std::unique_ptr<IRenderer> createRenderer(RendererBackend backend) {
    if (backend == RendererBackend::Vulkan) {
        return std::make_unique<VulkanRenderer>();
    }
    return std::make_unique<OpenGLRenderer>();
}

} // namespace iron3d
