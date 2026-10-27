// Traduccion unica donde se instancia la implementacion de VMA (header-only).
// No tocar: cualquier otro .cpp que incluya vk_mem_alloc.h lo hace sin VMA_IMPLEMENTATION.
//
// VMA_STATIC_VULKAN_FUNCTIONS por defecto (1): resuelve las funciones de Vulkan
// contra lo que ya enlaza Vulkan::Vulkan (vulkan-1.lib / libvulkan.so). No usamos
// volk en esta Fase 1, asi que no hace falta tocar nada de esto.
#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>
