#pragma once

// Instancia Vulkan, dispositivo fisico/logico, cola grafica y el descriptor
// pool que usa Dear ImGui. Fase 1: sin volk (se enlaza directo contra
// Vulkan::Vulkan via find_package(Vulkan)); sin capas de validacion activadas
// por defecto en release (se activan con IRON3D_VALIDATION=1 en debug, ver .cpp).

#include <vulkan/vulkan.h>
#include <vector>

namespace iron3d {

struct VulkanContext {
    VkInstance instance = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT debugMessenger = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    uint32_t graphicsQueueFamily = UINT32_MAX;
    VkQueue graphicsQueue = VK_NULL_HANDLE;
    VkDescriptorPool imguiDescriptorPool = VK_NULL_HANDLE;

    // requiredExtensions: las que pide GLFW (glfwGetRequiredInstanceExtensions).
    void init(const std::vector<const char*>& requiredInstanceExtensions);
    void shutdown();
};

void vkCheck(VkResult result, const char* what);

} // namespace iron3d
