#pragma once

// Oclusion ambiental en espacio de pantalla (Fase 3, ver ROADMAP_RENDER.md):
// tres pasadas propias encadenadas, autocontenidas igual que ShadowMap --
// quien llama solo necesita beginGBufferPass/drawMeshGBuffer/endGBufferPass
// (mismo patron que la pasada de sombras); la SSAO y el blur que vienen
// detras corren solos dentro de endGBufferPass(), sin que main.cpp sepa que
// existen.
//
// 1. G-buffer: normal+posicion en espacio de VISTA de toda la malla opaca
//    (dos attachments de color RGBA16F + un depth-only descartable para el
//    test de profundidad entre objetos -- igual idea que SwapchainWindow,
//    pero a diferencia de ese, estos SI hay que poder muestrearlos despues).
// 2. SSAO: pasada de pantalla completa (sin vertex buffer, ver
//    shaders/fullscreen.vert) que lee el G-buffer y escribe oclusion cruda
//    en un solo canal (R8_UNORM).
// 3. Blur: caja 4x4 sobre el resultado crudo -- hace falta porque ssao.frag
//    usa una rotacion de ruido continua por pixel (ningun textura de ruido
//    nueva que subir), que sin suavizar se ve granulado.
//
// Las tres a resolucion de framebuffer completo (se recrean en resize(),
// igual que el depth buffer de SwapchainWindow) -- no la resolucion fija de
// ShadowMap, que es independiente del tamano de ventana.

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <filesystem>

namespace iron3d {

class SsaoPass {
public:
    void create(VkDevice device, VkPhysicalDevice physicalDevice, VmaAllocator allocator,
                const std::filesystem::path& shaderDir, int width, int height);
    void destroy(VkDevice device, VmaAllocator allocator);
    // Recrea solo las imagenes/framebuffers (mismo formato, nuevo tamano) --
    // render passes y pipelines no dependen del tamano, se quedan igual.
    void resize(VkDevice device, VkPhysicalDevice physicalDevice, VmaAllocator allocator, int width, int height);

    // Abre el render pass del G-buffer y deja su pipeline ya enlazada +
    // view/proj ya en los push constants -- el caller solo tiene que llamar
    // drawMeshGBuffer() por cada malla de la escena (mismo recorrido que
    // beginShadowPass/drawMeshShadow de ShadowMap).
    void beginGBufferPass(VkCommandBuffer cmd, const glm::mat4& view, const glm::mat4& proj);
    void drawMeshGBuffer(VkCommandBuffer cmd, const glm::mat4& model) const;
    // Cierra el G-buffer y ENCADENA la pasada de SSAO + la de blur sobre el
    // mismo command buffer -- al volver de aqui, descriptorSet() ya apunta
    // al resultado final difuminado, listo para que LitPipeline lo muestree.
    void endGBufferPass(VkCommandBuffer cmd);

    VkDescriptorSetLayout descriptorSetLayout() const { return aoOutputSetLayout_; }
    VkDescriptorSet descriptorSet() const { return aoOutputSet_; }

    // Radio (unidades de escena, 1 = 200cm), bias (anti-acne) y exponente de
    // intensidad del efecto -- expuestos por si Core quiere un panel de
    // ajuste mas adelante; de momento son los valores por defecto fijos de
    // create().
    float radius = 0.5f;
    float bias = 0.025f;
    float power = 1.0f;

private:
    struct PushConstantsGBuffer {
        glm::mat4 model;
        glm::mat4 view;
        glm::mat4 proj;
    };
    struct PushConstantsSsao {
        glm::mat4 proj;
        glm::vec4 params; // x=radius, y=bias, z=power, w=sin uso
    };

    void createImages(VkDevice device, VmaAllocator allocator);
    void destroyImages(VkDevice device, VmaAllocator allocator);
    void createGBufferPass(VkDevice device);
    void createSsaoPass(VkDevice device);
    void createBlurPass(VkDevice device);
    void createFramebuffers(VkDevice device);
    void destroyFramebuffers(VkDevice device);
    void createGBufferPipeline(VkDevice device, const std::filesystem::path& shaderDir);
    void createSsaoPipeline(VkDevice device, const std::filesystem::path& shaderDir);
    void createBlurPipeline(VkDevice device, const std::filesystem::path& shaderDir);
    void createDescriptors(VkDevice device);
    void writeDescriptors(VkDevice device);

    // Vista/proyeccion cacheadas entre beginGBufferPass() y cada
    // drawMeshGBuffer() posterior -- LitPipeline empuja el push constant
    // completo en cada draw en vez de partirlo en varios rangos, mismo
    // convenio que su propio LitPushConstants monolitico.
    glm::mat4 gbufferView_{1.0f};
    glm::mat4 gbufferProj_{1.0f};

    int width_ = 0, height_ = 0;
    VkFormat gbufferColorFormat_ = VK_FORMAT_R16G16B16A16_SFLOAT;
    VkFormat aoFormat_ = VK_FORMAT_R8_UNORM;
    VkFormat gbufferDepthFormat_ = VK_FORMAT_UNDEFINED;

    // --- G-buffer ---
    VkImage gPosImage_ = VK_NULL_HANDLE, gNormalImage_ = VK_NULL_HANDLE, gDepthImage_ = VK_NULL_HANDLE;
    VmaAllocation gPosAlloc_ = VK_NULL_HANDLE, gNormalAlloc_ = VK_NULL_HANDLE, gDepthAlloc_ = VK_NULL_HANDLE;
    VkImageView gPosView_ = VK_NULL_HANDLE, gNormalView_ = VK_NULL_HANDLE, gDepthView_ = VK_NULL_HANDLE;
    VkRenderPass gbufferRenderPass_ = VK_NULL_HANDLE;
    VkFramebuffer gbufferFramebuffer_ = VK_NULL_HANDLE;
    VkPipeline gbufferPipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout gbufferLayout_ = VK_NULL_HANDLE;

    // --- SSAO (lee G-buffer, escribe aoRaw) ---
    VkImage aoRawImage_ = VK_NULL_HANDLE;
    VmaAllocation aoRawAlloc_ = VK_NULL_HANDLE;
    VkImageView aoRawView_ = VK_NULL_HANDLE;
    VkRenderPass ssaoRenderPass_ = VK_NULL_HANDLE;
    VkFramebuffer ssaoFramebuffer_ = VK_NULL_HANDLE;
    VkPipeline ssaoPipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout ssaoLayout_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout ssaoInputSetLayout_ = VK_NULL_HANDLE;
    VkDescriptorSet ssaoInputSet_ = VK_NULL_HANDLE;

    // --- Blur (lee aoRaw, escribe aoBlurred -- el resultado final expuesto) ---
    VkImage aoBlurredImage_ = VK_NULL_HANDLE;
    VmaAllocation aoBlurredAlloc_ = VK_NULL_HANDLE;
    VkImageView aoBlurredView_ = VK_NULL_HANDLE;
    VkRenderPass blurRenderPass_ = VK_NULL_HANDLE;
    VkFramebuffer blurFramebuffer_ = VK_NULL_HANDLE;
    VkPipeline blurPipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout blurLayout_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout blurInputSetLayout_ = VK_NULL_HANDLE;
    VkDescriptorSet blurInputSet_ = VK_NULL_HANDLE;

    VkSampler linearSampler_ = VK_NULL_HANDLE; // comun a las tres lecturas (G-buffer, aoRaw, aoBlurred)

    // Set que consume LitPipeline (envuelve aoBlurredView_).
    VkDescriptorSetLayout aoOutputSetLayout_ = VK_NULL_HANDLE;
    VkDescriptorSet aoOutputSet_ = VK_NULL_HANDLE;

    VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE; // comun a los 3 sets de arriba
};

} // namespace iron3d
