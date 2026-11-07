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

# stb_image (dominio publico / MIT), fijado por commit: miniaturas JPEG y
# descompresion zlib de las librerias .lib4d. Solo cabeceras: no tiene
# CMakeLists propio, FetchContent solo lo descarga.
FetchContent_Declare(stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG        d14d46f6b44456eb88beac58cba833a4135d4c89)

FetchContent_MakeAvailable(glfw glm vma imgui stb)

# Lector y escritor de .lib4d (importers/lib4d): lo comparten Iron3D y las pruebas.
add_library(iron3d_lib4d STATIC
    src/importers/lib4d/PresetLibrary.cpp
    src/importers/lib4d/PresetLibraryWriter.cpp
    src/importers/lib4d/StbImpl.cpp)
target_include_directories(iron3d_lib4d PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_include_directories(iron3d_lib4d SYSTEM PUBLIC ${stb_SOURCE_DIR})
if(MSVC)
    target_compile_options(iron3d_lib4d PRIVATE /W4 /utf-8)
    # stb_image no es nuestro: sin avisos en su implementacion.
    set_source_files_properties(src/importers/lib4d/StbImpl.cpp PROPERTIES COMPILE_OPTIONS "/W0")
else()
    target_compile_options(iron3d_lib4d PRIVATE -Wall -Wextra)
    set_source_files_properties(src/importers/lib4d/StbImpl.cpp PROPERTIES COMPILE_OPTIONS "-w")
endif()

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
    ${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp)
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
    src/gfx/IRenderer.cpp
    src/gfx/VulkanRenderer.cpp
    src/gfx/ShadowPipeline.cpp
    src/gfx/ShadowMap.cpp
    src/gfx/SsaoPass.cpp
    src/gfx/OpenGLRenderer.cpp
    src/gfx/gl/GLLoader.cpp
    src/core/MeshData.cpp
    src/core/MeshTopology.cpp
    src/core/MeshEdit.cpp
    src/core/PrimitiveMesh.cpp
    src/core/OrbitalCamera.cpp
    src/core/SceneNode.cpp
    src/core/SceneSerializer.cpp
    src/core/SceneHistory.cpp
    src/core/TransformGizmo.cpp
    src/core/GizmoMesh.cpp
    src/core/Paths.cpp
    src/core/Preferences.cpp
    src/core/Material.cpp
    src/core/ThumbnailRenderer.cpp
    src/gfx/LitPipeline.cpp
    src/ui/MainUi.cpp
    src/ui/UiTheme.cpp
    src/ui/C4DIcons.cpp
    src/ui/IconsSplines.cpp
    src/ui/IconsGenerators.cpp
    src/ui/IconsDeformers.cpp
    src/ui/IconsScene.cpp
    src/ui/IconsMenus.cpp
    src/ui/UiPreset.cpp
    src/ui/FileDialog.cpp
    src/ui/ContentBrowser.cpp
    src/importers/c4d_compat/C4DGeometry.cpp)

target_include_directories(Iron3DStudio PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${glm_SOURCE_DIR})

# -----------------------------------------------------------------------------
# Importador de .c4d (Cineware SDK de MAXON). El SDK NO esta en el repositorio
# (ver AGENTS.md 7): se pasa su carpeta con -DIRON3D_CINEWARE_SDK=<ruta>, la
# que contiene includes/ y libraries/. Sin ella (CI, Linux) se compila la
# version vacia del importador y Iron3D explica que no puede abrir .c4d.
# MAXON solo publica librerias para Windows y macOS.
# -----------------------------------------------------------------------------
set(IRON3D_CINEWARE_SDK "" CACHE PATH "Carpeta del Cineware SDK (includes/, libraries/) para importar .c4d")
if(IRON3D_CINEWARE_SDK AND WIN32)
    if(NOT EXISTS "${IRON3D_CINEWARE_SDK}/includes/c4d.h")
        message(FATAL_ERROR "IRON3D_CINEWARE_SDK no apunta al Cineware SDK (falta includes/c4d.h): ${IRON3D_CINEWARE_SDK}")
    endif()
    set(_cwlib "${IRON3D_CINEWARE_SDK}/libraries/win")
    target_sources(Iron3DStudio PRIVATE src/importers/cineware/CinewareImporter.cpp
        src/importers/cineware/CinewareExporter.cpp)
    # SYSTEM: los avisos de las cabeceras del SDK no son nuestros (/W4 sigue para nuestro codigo).
    target_include_directories(Iron3DStudio SYSTEM PRIVATE "${IRON3D_CINEWARE_SDK}/includes")
    set_source_files_properties(src/importers/cineware/CinewareImporter.cpp
        src/importers/cineware/CinewareExporter.cpp PROPERTIES
        COMPILE_DEFINITIONS "__PC;_CRT_SECURE_NO_WARNINGS"
        # Las cabeceras del SDK no compilan con /permissive- (el modo estricto que
        # trae C++20 en MSVC): solo en este archivo se vuelve al modo permisivo.
        COMPILE_OPTIONS "/permissive;/Zc:twoPhase-")
    # Librerias de VS2019 (/MD), compatibles con el MSVC actual.
    target_link_libraries(Iron3DStudio PRIVATE
        "$<IF:$<CONFIG:Debug>,${_cwlib}/cinewarelib_2019/cinewarelib_debug.lib,${_cwlib}/cinewarelib_2019/cinewarelib_release.lib>"
        "$<IF:$<CONFIG:Debug>,${_cwlib}/jpeglib_2019/jpeglib_debug.lib,${_cwlib}/jpeglib_2019/jpeglib_release.lib>"
        ws2_32 winmm rpcrt4 iphlpapi)
    message(STATUS "Iron3D: importador .c4d con Cineware SDK (${IRON3D_CINEWARE_SDK})")

    # Herramienta de consola para probar el importador con archivos reales.
    add_executable(c4d_probe
        tools/c4d_probe.cpp
        src/importers/cineware/CinewareImporter.cpp
        src/importers/cineware/CinewareExporter.cpp
        src/importers/c4d_compat/C4DGeometry.cpp
        src/core/MeshData.cpp
        src/core/MeshTopology.cpp
        src/core/SceneNode.cpp
        src/core/SceneSerializer.cpp
        src/core/Material.cpp
        src/core/ThumbnailRenderer.cpp)
    target_include_directories(c4d_probe PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${glm_SOURCE_DIR})
    target_include_directories(c4d_probe SYSTEM PRIVATE "${IRON3D_CINEWARE_SDK}/includes")
    target_compile_definitions(c4d_probe PRIVATE GLM_FORCE_RADIANS)
    target_compile_options(c4d_probe PRIVATE /W4 /utf-8)
    target_link_libraries(c4d_probe PRIVATE iron3d_lib4d
        "$<IF:$<CONFIG:Debug>,${_cwlib}/cinewarelib_2019/cinewarelib_debug.lib,${_cwlib}/cinewarelib_2019/cinewarelib_release.lib>"
        "$<IF:$<CONFIG:Debug>,${_cwlib}/jpeglib_2019/jpeglib_debug.lib,${_cwlib}/jpeglib_2019/jpeglib_release.lib>"
        ws2_32 winmm rpcrt4 iphlpapi)
else()
    if(IRON3D_CINEWARE_SDK)
        message(WARNING "IRON3D_CINEWARE_SDK solo se usa en Windows por ahora; se compila sin importador .c4d")
    endif()
    target_sources(Iron3DStudio PRIVATE src/importers/cineware/CinewareImporterStub.cpp)
endif()

target_link_libraries(Iron3DStudio PRIVATE
    Vulkan::Vulkan
    glfw
    imgui
    vma_impl
    iron3d_lib4d)

target_compile_definitions(Iron3DStudio PRIVATE
    GLM_FORCE_RADIANS
    GLM_FORCE_DEPTH_ZERO_TO_ONE)

if(MSVC)
    # /utf-8: los textos de la UI llevan tildes (Edición, Selección...).
    target_compile_options(Iron3DStudio PRIVATE /W4 /utf-8)
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
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/basic.frag
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/lit.vert
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/lit.frag
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/shadow.vert
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/fullscreen.vert
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/gbuffer.vert
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/gbuffer.frag
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/ssao.frag
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/blur.frag)

add_custom_command(TARGET Iron3DStudio POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:Iron3DStudio>/shaders"
    COMMENT "Preparando carpeta de shaders junto al binario")

foreach(SHADER ${SHADER_SOURCES})
    get_filename_component(SHADER_NAME ${SHADER} NAME)
    add_custom_command(TARGET Iron3DStudio POST_BUILD
        COMMAND ${GLSLANG_VALIDATOR} -V ${SHADER} -o "$<TARGET_FILE_DIR:Iron3DStudio>/shaders/${SHADER_NAME}.spv"
        COMMENT "Compilando ${SHADER_NAME} -> SPIR-V")
endforeach()

# -----------------------------------------------------------------------------
# Pruebas sin GPU (las ejecuta tambien el CI): geometria compatible con C4D.
# -----------------------------------------------------------------------------
enable_testing()
add_executable(iron3d_tests
    tests/c4d_geometry_test.cpp
    src/importers/c4d_compat/C4DGeometry.cpp
    src/core/MeshData.cpp
    src/core/MeshTopology.cpp
    src/core/MeshEdit.cpp
    src/core/SceneNode.cpp
    src/core/Material.cpp
    src/core/SceneSerializer.cpp
    src/core/ThumbnailRenderer.cpp)
target_include_directories(iron3d_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${glm_SOURCE_DIR})
target_compile_definitions(iron3d_tests PRIVATE GLM_FORCE_RADIANS)
if(MSVC)
    target_compile_options(iron3d_tests PRIVATE /W4 /utf-8)
else()
    target_compile_options(iron3d_tests PRIVATE -Wall -Wextra)
endif()
add_test(NAME c4d_geometry COMMAND iron3d_tests)

add_executable(preset_library_test tests/preset_library_test.cpp)
target_link_libraries(preset_library_test PRIVATE iron3d_lib4d)
if(MSVC)
    target_compile_options(preset_library_test PRIVATE /W4 /utf-8)
else()
    target_compile_options(preset_library_test PRIVATE -Wall -Wextra)
endif()
add_test(NAME preset_library COMMAND preset_library_test)
