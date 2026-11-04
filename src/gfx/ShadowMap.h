#pragma once

// Mapa de sombras unico (Fase render 2, ver ROADMAP_RENDER.md): imagen de
// profundidad + render pass + framebuffer propios (independientes del
// swapchain, no cambian de tamano al redimensionar la ventana), mas el
// sampler/descriptor set que lit.frag usa para muestrearlo. Trata la unica
// luz de la escena como si "mirara" al origen (ver computeLightViewProj en
// main.cpp) -- no es un point light omnidireccional (eso necesitaria 6
// pasadas a un cubemap, fuera de alcance de esta fase).

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <filesystem>

#include "ShadowPipeline.h"

namespace iron3d {

class ShadowMap {
public:
    // Resolucion fija, no ligada al tamano de ventana -- 2048 es un punto
    // medio razonable (sombras nitidas sin gastar demasiada VRAM) para una
    // escena de un puñado de primitivas cerca del origen.
    static constexpr uint32_t kResolution = 2048;

    void create(VkDevice device, VkPhysicalDevice physicalDevice, VmaAllocator allocator,
                const std::filesystem::path& shaderDir);
    void destroy(VkDevice device, VmaAllocator allocator);

    // Abre el render pass de sombras (framebuffer/viewport propios,
    // kResolution x kResolution) y deja la pipeline de sombras ya
    // enlazada -- el caller solo tiene que llamar pushLightMvp()+dibujar
    // por cada malla.
    void beginPass(VkCommandBuffer cmd) const;
    void pushLightMvp(VkCommandBuffer cmd, const glm::mat4& lightMvp) const;
    // Cierra el render pass -- el finalLayout del attachment (ver .cpp) ya
    // deja la imagen en VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, lista
    // para que lit.frag la muestree en la pasada principal sin necesitar
    // una barrera manual aparte.
    void endPass(VkCommandBuffer cmd) const;

    VkDescriptorSetLayout descriptorSetLayout() const { return descriptorSetLayout_; }
    VkDescriptorSet descriptorSet() const { return descriptorSet_; }

private:
    ShadowPipeline pipeline_;

    VkImage image_ = VK_NULL_HANDLE;
    VmaAllocation allocation_ = VK_NULL_HANDLE;
    VkImageView view_ = VK_NULL_HANDLE;
    VkSampler sampler_ = VK_NULL_HANDLE;
    VkFormat format_ = VK_FORMAT_UNDEFINED;

    VkRenderPass renderPass_ = VK_NULL_HANDLE;
    VkFramebuffer framebuffer_ = VK_NULL_HANDLE;

    VkDescriptorSetLayout descriptorSetLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
    VkDescriptorSet descriptorSet_ = VK_NULL_HANDLE;
};

} // namespace iron3d
