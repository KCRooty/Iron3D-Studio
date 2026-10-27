#pragma once

// Envuelve ImGui_ImplVulkanH_Window (swapchain + render pass + framebuffers +
// sincronizacion) -- son los helpers "H_" del propio backend de ImGui, pensados
// justo para esto (ver imgui_impl_vulkan.h). Adaptado del patron FrameRender/
// FramePresent de examples/example_glfw_vulkan/main.cpp de Dear ImGui, partido
// en dos mitades (beginFrame/endFrame) para poder meter el dibujado del cubo de
// Iron3D DENTRO del mismo render pass, antes de componer la UI de ImGui encima.

#include <vulkan/vulkan.h>

struct ImDrawData;
struct ImGui_ImplVulkanH_Window;

namespace iron3d {

class SwapchainWindow {
public:
    void init(VkInstance instance, VkPhysicalDevice physicalDevice, VkDevice device,
              uint32_t queueFamily, VkSurfaceKHR surface, int width, int height,
              uint32_t minImageCount);
    void destroy(VkInstance instance, VkDevice device);

    // Llamar cada frame tras glfwGetFramebufferSize; reconstruye el swapchain
    // si el tamano cambio o si un frame anterior pidio rebuild (requestRebuild()).
    void resizeIfNeeded(VkInstance instance, VkPhysicalDevice physicalDevice, VkDevice device,
                         uint32_t queueFamily, int fbWidth, int fbHeight);

    // Adquiere imagen, espera el fence del frame, abre el command buffer y el
    // render pass. Devuelve VK_NULL_HANDLE si hay que saltarse el frame (p.ej.
    // swapchain fuera de fecha -- ya se marco requestRebuild() internamente).
    VkCommandBuffer beginFrame(VkDevice device);

    // Cierra el render pass (tras que el caller haya grabado ImGui_ImplVulkan_RenderDrawData
    // y cualquier otro dibujado), envia el command buffer y presenta.
    void endFrame(VkDevice device, VkQueue queue, ImDrawData* imguiDrawData);

    void setClearColor(float r, float g, float b, float a);

    VkRenderPass renderPass() const;
    uint32_t imageCount() const;
    int width() const;
    int height() const;
    bool isMinimized() const { return width() <= 0 || height() <= 0; }

    ImGui_ImplVulkanH_Window* raw() { return wd_; }

private:
    ImGui_ImplVulkanH_Window* wd_ = nullptr; // propiedad nuestra, ver .cpp
    bool swapChainRebuild_ = false;
    uint32_t minImageCount_ = 2;
};

} // namespace iron3d
