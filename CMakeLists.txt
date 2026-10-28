cmake_minimum_required(VERSION 3.24)
project(Iron3DStudio LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# -----------------------------------------------------------------------------
# Fase 1 (ver AGENTS.md §5 y el Documento Maestro §8.1):
#   ventana GLFW, contexto Vulkan 1.3, VMA, Dear ImGui, cubo basico + camara
#   orbital estilo C4D (Alt + clics). Windows (MSVC) y Linux (GCC/Clang) con
#   paridad -- CI (GitHub Actions) debe compilar ambos o la build falla.
# -----------------------------------------------------------------------------

find_package(Vulkan REQUIRED)

include(FetchContent)

FetchContent_Declare(glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG        3.4
    GIT_SHALLOW    TRUE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
# Sin Wayland por ahora (Fase 1-2, Linux solo via X11): evita depender de
# wayland-scanner/wayland-protocols, que no siempre estan instalados (p.ej.
# el runner de GitHub Actions ni la imagen base de WSL2 Debian los traen).
set(GLFW_BUILD_WAYLAND OFF CACHE BOOL "" FORCE)

FetchContent_Declare(glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG        1.0.1
    GIT_SHALLOW    TRUE)

FetchContent_Declare(vma
    GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git
    GIT_TAG        v3.1.0
    GIT_SHALLOW    TRUE)

FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.91.0
    GIT_SHALLOW    TRUE)

FetchContent_MakeAvailable(glfw glm vma imgui)

# VMA es header-only: la implementacion se instancia en src/gfx/VulkanMemory.cpp
add_library(vma_impl STATIC ${CMAKE_CURRENT_SOURCE_DIR}/src/gfx/VmaImpl.cpp)
target_include_directories(vma_impl PUBLIC ${vma_SOURCE_DIR}/include)
target_link_libraries(vma_impl PUBLIC Vulkan::Vulkan)

# Dear ImGui (core + backends GLFW/Vulkan) -- no tiene CMakeLists.txt propio
add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp)
target_include_directories(imgui PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends)
target_link_libraries(imgui PUBLIC glfw Vulkan::Vulkan)

# -----------------------------------------------------------------------------
# Iron3D Studio (ejecutable) -- se declara ANTES del bloque de shaders: un
# add_custom_command(OUTPUT ...) que usa $<TARGET_FILE_DIR:Iron3DStudio> exige
# que el target ya exista en el momento en que CMake procesa esa llamada (a
# diferencia de otros usos de generator expressions, que se resuelven en
# tiempo de generate sin importar el orden -- este caso concreto no; fallo real
# de esta sesion: "No target Iron3DStudio" hasta mover esto aqui arriba).
# -----------------------------------------------------------------------------
add_executable(Iron3DStudio
    src/main.cpp
    src/gfx/VulkanContext.cpp
    src/gfx/SwapchainWindow.cpp
    src/gfx/BasicPipeline.cpp
    src/core/MeshData.cpp
    src/core/PrimitiveMesh.cpp
    src/core/OrbitalCamera.cpp
    src/core/SceneNode.cpp
    src/core/SceneSerializer.cpp
    src/core/TransformGizmo.cpp
    src/core/GizmoMesh.cpp
    src/ui/MainUi.cpp)

target_include_directories(Iron3DStudio PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${glm_SOURCE_DIR})

target_link_libraries(Iron3DStudio PRIVATE
    Vulkan::Vulkan
    glfw
    imgui
    vma_impl)

target_compile_definitions(Iron3DStudio PRIVATE
    GLM_FORCE_RADIANS
    GLM_FORCE_DEPTH_ZERO_TO_ONE)

if(MSVC)
    target_compile_options(Iron3DStudio PRIVATE /W4)
else()
    target_compile_options(Iron3DStudio PRIVATE -Wall -Wextra)
endif()

# -----------------------------------------------------------------------------
# Shaders: GLSL -> SPIR-V con glslangValidator (viene del Vulkan SDK).
# POST_BUILD sobre Iron3DStudio (no un add_custom_command(OUTPUT...) suelto):
# $<TARGET_FILE_DIR:...> en el campo OUTPUT de un custom command "suelto" no
# esta soportado por CMake (falla en generate con "No target ...", da igual el
# orden del archivo -- comprobado en esta sesion); en la forma POST_BUILD,
# atada al propio target, si lo esta. Recompila los 2 shaders en cada build
# del target (son 2 archivos minusculos, el coste es nada en Fase 1).
# -----------------------------------------------------------------------------
find_program(GLSLANG_VALIDATOR
    NAMES glslangValidator glslangValidator.exe
    HINTS "$ENV{VULKAN_SDK}/Bin" "$ENV{VULKAN_SDK}/bin")
if(NOT GLSLANG_VALIDATOR)
    message(FATAL_ERROR "glslangValidator no encontrado -- instala el Vulkan SDK (VULKAN_SDK sin definir)")
endif()

set(SHADER_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/basic.vert
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/basic.frag)

add_custom_command(TARGET Iron3DStudio POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:Iron3DStudio>/shaders"
    COMMENT "Preparando carpeta de shaders junto al binario")

foreach(SHADER ${SHADER_SOURCES})
    get_filename_component(SHADER_NAME ${SHADER} NAME)
    add_custom_command(TARGET Iron3DStudio POST_BUILD
        COMMAND ${GLSLANG_VALIDATOR} -V ${SHADER} -o "$<TARGET_FILE_DIR:Iron3DStudio>/shaders/${SHADER_NAME}.spv"
        COMMENT "Compilando ${SHADER_NAME} -> SPIR-V")
endforeach()
