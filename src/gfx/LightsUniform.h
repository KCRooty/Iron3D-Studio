#pragma once

// Luces de la escena para el visor Vulkan (2026-11-10): un uniform buffer
// (set=3 de LitPipeline) con todas las luces y la matriz del mapa de sombras.
// Se rellena una vez por frame con vkCmdUpdateBuffer, fuera de cualquier
// render pass (entre beginFrame y la pasada de sombras). Un solo buffer
// basta: las barreras antes y despues de la copia ordenan la escritura
// frente a las lecturas del frame anterior, que van antes en la cola.

#include "core/SceneLighting.h"

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>

namespace iron3d {

// Mismo layout std140 que el bloque SceneLights de lit.vert/lit.frag.
struct GpuLight {
    glm::vec4 posType;     // xyz posicion, w tipo (0 puntual, 1 foco, 2 infinita)
    glm::vec4 dirFalloff;  // xyz direccion de emision, w modo de caida
    glm::vec4 colorRadius; // xyz color * intensidad, w radio de caida
    glm::vec4 cone;        // x cos exterior, y cos interior, z radio interior, w 1 difusion | 2 especular
};
struct GpuSceneLights {
    glm::mat4 lightViewProj;
    glm::vec4 info; // x numero de luces, y luz con sombra (-1 ninguna), z ambiente
    GpuLight lights[kMaxViewportLights];
};

// Ambiente del visor: con la luz por defecto, el de siempre (0.15); con luces
// de escena, bajo, para que se note de donde viene la luz (en C4D, sin
// iluminacion global, la cara que no recibe luz queda casi negra).
constexpr float kAmbientDefaultLight = 0.15f;
constexpr float kAmbientSceneLights = 0.04f;

GpuSceneLights packSceneLights(const SceneLighting& lighting, const glm::mat4& lightViewProj);

class LightsUniform {
public:
    void create(VkDevice device, VmaAllocator allocator);
    void destroy(VkDevice device, VmaAllocator allocator);

    VkDescriptorSetLayout descriptorSetLayout() const { return setLayout_; }
    VkDescriptorSet descriptorSet() const { return set_; }
    // Graba la copia y sus barreras en `cmd` (sin render pass abierto).
    void update(VkCommandBuffer cmd, const GpuSceneLights& data);

private:
    VkBuffer buffer_ = VK_NULL_HANDLE;
    VmaAllocation alloc_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout setLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool pool_ = VK_NULL_HANDLE;
    VkDescriptorSet set_ = VK_NULL_HANDLE;
};

} // namespace iron3d
