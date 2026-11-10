#pragma once

// Texturas de material en el visor Vulkan (2026-11-10, P1.1 del traspaso:
// "texturas en el visor"). Cache por ruta: cada imagen se lee del disco y se
// sube a la GPU UNA vez, la primera vez que una malla la pide, con su cadena
// de mipmaps (blit) y en formato sRGB (el muestreo devuelve lineal, que es lo
// que espera el PBR de lit.frag). Cada textura tiene su descriptor set listo
// para el set=2 de LitPipeline.
//
// Lo que NO hace todavia: liberar texturas que ya no usa ningun material
// (viven hasta cerrar el programa), cargar en segundo plano (la primera
// aparicion de un modelo grande da un tiron) ni EXR.

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>

#include <string>
#include <unordered_map>
#include <vector>

namespace iron3d {

class MaterialTextures {
public:
    void create(VkDevice device, VkPhysicalDevice physicalDevice, VmaAllocator allocator, VkQueue queue,
                uint32_t queueFamily);
    void destroy(VkDevice device, VmaAllocator allocator);

    VkDescriptorSetLayout descriptorSetLayout() const { return setLayout_; }
    // Textura blanca de 1x1: lo que se enlaza cuando la malla no lleva textura
    // (el set=2 tiene que estar siempre enlazado aunque el shader no lo lea).
    VkDescriptorSet whiteSet() const { return white_.set; }
    // Descriptor de la textura `rawPath` (tal como la guarda el material,
    // resuelta contra `baseDir`). VK_NULL_HANDLE si no se encuentra o no se
    // puede leer; el fallo tambien queda en la cache para no reintentarlo
    // en cada frame.
    VkDescriptorSet get(const std::string& rawPath, const std::string& baseDir);

    size_t loadedCount() const { return textures_.size(); }

private:
    struct Texture {
        VkImage image = VK_NULL_HANDLE;
        VmaAllocation alloc = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VkDescriptorSet set = VK_NULL_HANDLE;
    };
    bool upload(const uint8_t* rgba, int width, int height, Texture& out);
    void freeTexture(const Texture& t);

    VkDevice device_ = VK_NULL_HANDLE;
    VmaAllocator allocator_ = VK_NULL_HANDLE;
    VkQueue queue_ = VK_NULL_HANDLE;
    VkCommandPool pool_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout setLayout_ = VK_NULL_HANDLE;
    VkSampler sampler_ = VK_NULL_HANDLE;
    bool canBlitMips_ = false;

    Texture white_;
    std::vector<Texture> textures_;
    std::unordered_map<std::string, int> cache_; // clave -> indice en textures_, -1 = no se pudo cargar
};

} // namespace iron3d
