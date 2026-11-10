#include "OpenGLRenderer.h"
#include "core/TextureImage.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <cstdio>
#include <cstdlib>
#include <cstddef>
#include <algorithm>

namespace iron3d {

namespace {

// Mismo GLSL logico que shaders/basic.vert/.frag (ver BasicPipeline.cpp):
// MVP * posicion, color interpolado tal cual. La unica diferencia real es
// `uniform mat4 uMvp` en vez del push constant de Vulkan -- OpenGL no tiene
// ese concepto, un uniform normal cumple la misma funcion aqui.
const char* kVertexSrc = R"GLSL(#version 330 core
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;
uniform mat4 uMvp;
out vec3 fragColor;
void main() {
    gl_Position = uMvp * vec4(inPosition, 1.0);
    fragColor = inColor;
}
)GLSL";

const char* kFragmentSrc = R"GLSL(#version 330 core
in vec3 fragColor;
out vec4 outColor;
void main() {
    outColor = vec4(fragColor, 1.0);
}
)GLSL";

// Mismo GLSL logico que shaders/lit.vert/.frag (ver LitPipeline.cpp): Phong
// basico, ambiente fijo + una luz blanca puntual, sin texturas/sombras.
// Uniforms en vez de push constants -- OpenGL no tiene ese concepto, y
// tampoco el limite de 128/176 bytes que obligo a empaquetar todo en vec4
// en el lado Vulkan, asi que aqui cada dato va suelto y mas legible. La
// intensidad ambiente (0.15, hardcodeada abajo) es el mismo valor que
// kAmbientStrength en LitPipeline.cpp (Vulkan).

const char* kLitVertexSrc = R"GLSL(#version 330 core
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;
uniform mat4 uModel;
uniform mat4 uMvp;
out vec3 fragNormalWS;
out vec3 fragPosWS;
out vec2 fragUV;
void main() {
    vec4 worldPos = uModel * vec4(inPosition, 1.0);
    gl_Position = uMvp * vec4(inPosition, 1.0);
    fragNormalWS = normalize(mat3(uModel) * inNormal);
    fragPosWS = worldPos.xyz;
    fragUV = inUV;
}
)GLSL";

// Textura de Color (2026-11-10): mismas reglas que lit.frag -- sustituye al
// color difuso; sin Mosaico, fuera de 0..1 se ve el color del material. Aqui
// todo va en espacio gamma (el Phong de este backend nunca paso a lineal),
// asi que la textura se sube tal cual, sin formato sRGB.
const char* kLitFragmentSrc = R"GLSL(#version 330 core
in vec3 fragNormalWS;
in vec3 fragPosWS;
in vec2 fragUV;
uniform vec3 uLightPos;
uniform vec3 uEyePos;
uniform vec3 uMaterialDiffuse;
uniform float uMaterialSpecular;
uniform float uMaterialShininess;
uniform sampler2D uColorTex;
uniform int uTexFlags;     // 1 = hay textura de Color, 2 = Mosaico
uniform vec4 uUvTransform; // uv de la imagen = uv * xy + zw
out vec4 outColor;
void main() {
    vec3 N = normalize(fragNormalWS);
    vec3 L = normalize(uLightPos - fragPosWS);
    vec3 V = normalize(uEyePos - fragPosWS);
    vec3 R = reflect(-L, N);
    const vec3 lightColor = vec3(1.0);

    vec3 albedo = uMaterialDiffuse;
    if ((uTexFlags & 1) != 0) {
        vec2 uv = fragUV * uUvTransform.xy + uUvTransform.zw;
        vec3 texel = texture(uColorTex, uv).rgb;
        bool inside = all(greaterThanEqual(uv, vec2(0.0))) && all(lessThanEqual(uv, vec2(1.0)));
        if ((uTexFlags & 2) != 0 || inside) albedo = texel;
    }

    vec3 ambient = 0.15 * albedo;
    float diff = max(dot(N, L), 0.0);
    vec3 diffuse = diff * albedo * lightColor;
    float spec = (diff > 0.0) ? pow(max(dot(R, V), 0.0), max(uMaterialShininess, 1.0)) : 0.0;
    vec3 specular = uMaterialSpecular * spec * lightColor;

    outColor = vec4(ambient + diffuse + specular, 1.0);
}
)GLSL";

gl::GLuint compileShader(gl::GLenum type, const char* src) {
    gl::GLuint shader = gl::glCreateShader(type);
    gl::glShaderSource(shader, 1, &src, nullptr);
    gl::glCompileShader(shader);
    gl::GLint ok = 0;
    gl::glGetShaderiv(shader, gl::GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        gl::GLsizei len = 0;
        gl::glGetShaderInfoLog(shader, (gl::GLsizei)sizeof(log), &len, log);
        std::fprintf(stderr, "[Iron3D][opengl] Error compilando shader: %.*s\n", (int)len, log);
        std::abort();
    }
    return shader;
}

gl::GLuint linkProgram(gl::GLuint vert, gl::GLuint frag) {
    gl::GLuint program = gl::glCreateProgram();
    gl::glAttachShader(program, vert);
    gl::glAttachShader(program, frag);
    gl::glLinkProgram(program);
    gl::GLint ok = 0;
    gl::glGetProgramiv(program, gl::GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024];
        gl::GLsizei len = 0;
        gl::glGetProgramInfoLog(program, (gl::GLsizei)sizeof(log), &len, log);
        std::fprintf(stderr, "[Iron3D][opengl] Error enlazando programa: %.*s\n", (int)len, log);
        std::abort();
    }
    return program;
}

// Layout de Vertex (ver core/Vertex.h): vec3 posicion + vec3 color, igual
// que el VkVertexInputAttributeDescription de BasicPipeline.cpp. Se asume
// que el VAO/VBO correspondiente ya esta bindeado.
void setupVertexAttribs() {
    gl::glEnableVertexAttribArray(0);
    gl::glVertexAttribPointer(0, 3, gl::GL_FLOAT, gl::GL_FALSE_, (gl::GLsizei)sizeof(Vertex),
                               (const void*)offsetof(Vertex, position));
    gl::glEnableVertexAttribArray(1);
    gl::glVertexAttribPointer(1, 3, gl::GL_FLOAT, gl::GL_FALSE_, (gl::GLsizei)sizeof(Vertex),
                               (const void*)offsetof(Vertex, color));
}

// Mismo Vertex, pero para litProgram_: location 0 = posicion, location 1 =
// normal (el color no se usa, lo da el material -- ver kLitVertexSrc).
void setupLitVertexAttribs() {
    gl::glEnableVertexAttribArray(0);
    gl::glVertexAttribPointer(0, 3, gl::GL_FLOAT, gl::GL_FALSE_, (gl::GLsizei)sizeof(Vertex),
                               (const void*)offsetof(Vertex, position));
    gl::glEnableVertexAttribArray(1);
    gl::glVertexAttribPointer(1, 3, gl::GL_FLOAT, gl::GL_FALSE_, (gl::GLsizei)sizeof(Vertex),
                               (const void*)offsetof(Vertex, normal));
    gl::glEnableVertexAttribArray(2); // UV (2026-11-10, texturas)
    gl::glVertexAttribPointer(2, 2, gl::GL_FLOAT, gl::GL_FALSE_, (gl::GLsizei)sizeof(Vertex),
                               (const void*)offsetof(Vertex, uv));
}

// Textura 2D con mipmaps y mosaico (REPEAT), RGBA8 tal cual.
gl::GLuint createRepeatTexture(const uint8_t* rgba, int width, int height) {
    gl::GLuint tex = 0;
    gl::glGenTextures(1, &tex);
    if (!tex) return 0;
    gl::glBindTexture(gl::GL_TEXTURE_2D, tex);
    gl::glTexParameteri(gl::GL_TEXTURE_2D, gl::GL_TEXTURE_MIN_FILTER, (gl::GLint)gl::GL_LINEAR_MIPMAP_LINEAR);
    gl::glTexParameteri(gl::GL_TEXTURE_2D, gl::GL_TEXTURE_MAG_FILTER, (gl::GLint)gl::GL_LINEAR);
    gl::glTexParameteri(gl::GL_TEXTURE_2D, gl::GL_TEXTURE_WRAP_S, (gl::GLint)gl::GL_REPEAT);
    gl::glTexParameteri(gl::GL_TEXTURE_2D, gl::GL_TEXTURE_WRAP_T, (gl::GLint)gl::GL_REPEAT);
    gl::glTexImage2D(gl::GL_TEXTURE_2D, 0, (gl::GLint)gl::GL_RGBA, width, height, 0, gl::GL_RGBA,
                     gl::GL_UNSIGNED_BYTE, rgba);
    gl::glGenerateMipmap(gl::GL_TEXTURE_2D);
    gl::glBindTexture(gl::GL_TEXTURE_2D, 0);
    return tex;
}

} // namespace

void OpenGLRenderer::init(GLFWwindow* window, int width, int height) {
    window_ = window;
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1); // vsync -- equivalente a VK_PRESENT_MODE_FIFO_KHR (SwapchainWindow)

    if (!gl::loadOpenGLFunctions()) {
        std::fprintf(stderr, "[Iron3D] Fallo cargando funciones OpenGL.\n");
        std::abort();
    }

    // Mismo cullMode=NONE que el backend Vulkan (ver el comentario de esa
    // decision en VulkanRenderer::init): sin verificar a mano el orden de
    // vertices de cada primitiva.
    gl::glDisable(gl::GL_CULL_FACE);

    // Buffer de profundidad (Fase 5b): GLFW ya reserva uno de serie en el
    // framebuffer por defecto (GLFW_DEPTH_BITS=24, nunca lo hemos hintado a
    // 0), solo hacia falta activarlo -- antes de esto, dos objetos que se
    // solaparan en espacio (p.ej. una Esfera creada como hijo de un Cubo en
    // el mismo origen) se dibujaban en el orden de llamada sin resolver que
    // superficie estaba mas cerca de la camara, dando un renderizado roto
    // ("como un teseracto"). Igual que en VulkanRenderer/LitPipeline.
    gl::glEnable(gl::GL_DEPTH_TEST);
    gl::glDepthFunc(gl::GL_LESS);

    gl::GLuint vert = compileShader(gl::GL_VERTEX_SHADER, kVertexSrc);
    gl::GLuint frag = compileShader(gl::GL_FRAGMENT_SHADER, kFragmentSrc);
    program_ = linkProgram(vert, frag);
    gl::glDeleteShader(vert);
    gl::glDeleteShader(frag);
    uMvpLocation_ = gl::glGetUniformLocation(program_, "uMvp");

    gl::GLuint litVert = compileShader(gl::GL_VERTEX_SHADER, kLitVertexSrc);
    gl::GLuint litFrag = compileShader(gl::GL_FRAGMENT_SHADER, kLitFragmentSrc);
    litProgram_ = linkProgram(litVert, litFrag);
    gl::glDeleteShader(litVert);
    gl::glDeleteShader(litFrag);
    uLitModelLoc_ = gl::glGetUniformLocation(litProgram_, "uModel");
    uLitMvpLoc_ = gl::glGetUniformLocation(litProgram_, "uMvp");
    uLitLightPosLoc_ = gl::glGetUniformLocation(litProgram_, "uLightPos");
    uLitEyePosLoc_ = gl::glGetUniformLocation(litProgram_, "uEyePos");
    uLitDiffuseLoc_ = gl::glGetUniformLocation(litProgram_, "uMaterialDiffuse");
    uLitSpecularLoc_ = gl::glGetUniformLocation(litProgram_, "uMaterialSpecular");
    uLitShininessLoc_ = gl::glGetUniformLocation(litProgram_, "uMaterialShininess");
    uLitColorTexLoc_ = gl::glGetUniformLocation(litProgram_, "uColorTex");
    uLitTexFlagsLoc_ = gl::glGetUniformLocation(litProgram_, "uTexFlags");
    uLitUvTransformLoc_ = gl::glGetUniformLocation(litProgram_, "uUvTransform");
    const uint8_t white[4] = {255, 255, 255, 255};
    whiteTexture_ = createRepeatTexture(white, 1, 1);

    createGizmoBuffers();

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330 core");

    resizeIfNeeded(width, height);
}

void OpenGLRenderer::shutdown() {
    for (auto& mesh : meshes_) {
        if (mesh.vao == 0) continue;
        gl::glDeleteVertexArrays(1, &mesh.vao);
        gl::glDeleteBuffers(1, &mesh.vbo);
        gl::glDeleteBuffers(1, &mesh.ebo);
    }
    meshes_.clear();

    gl::glDeleteVertexArrays(1, &gizmoLineVao_);
    gl::glDeleteBuffers(1, &gizmoLineVbo_);
    gl::glDeleteVertexArrays(1, &gizmoTriVao_);
    gl::glDeleteBuffers(1, &gizmoTriVbo_);
    gl::glDeleteVertexArrays(1, &gridVao_);
    gl::glDeleteBuffers(1, &gridVbo_);
    gl::glDeleteProgram(program_);
    gl::glDeleteProgram(litProgram_);

    if (!uiTextures_.empty()) gl::glDeleteTextures((gl::GLsizei)uiTextures_.size(), uiTextures_.data());
    uiTextures_.clear();
    for (const auto& [key, tex] : materialTextures_)
        if (tex) gl::glDeleteTextures(1, &tex);
    materialTextures_.clear();
    if (whiteTexture_) gl::glDeleteTextures(1, &whiteTexture_);
    whiteTexture_ = 0;

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
}

void* OpenGLRenderer::createUiTexture(const uint8_t* rgba, int width, int height) {
    if (!rgba || width <= 0 || height <= 0) return nullptr;
    gl::GLuint tex = 0;
    gl::glGenTextures(1, &tex);
    if (!tex) return nullptr;
    gl::glBindTexture(gl::GL_TEXTURE_2D, tex);
    gl::glTexParameteri(gl::GL_TEXTURE_2D, gl::GL_TEXTURE_MIN_FILTER, (gl::GLint)gl::GL_LINEAR);
    gl::glTexParameteri(gl::GL_TEXTURE_2D, gl::GL_TEXTURE_MAG_FILTER, (gl::GLint)gl::GL_LINEAR);
    gl::glTexParameteri(gl::GL_TEXTURE_2D, gl::GL_TEXTURE_WRAP_S, (gl::GLint)gl::GL_CLAMP_TO_EDGE);
    gl::glTexParameteri(gl::GL_TEXTURE_2D, gl::GL_TEXTURE_WRAP_T, (gl::GLint)gl::GL_CLAMP_TO_EDGE);
    gl::glTexImage2D(gl::GL_TEXTURE_2D, 0, (gl::GLint)gl::GL_RGBA, width, height, 0, gl::GL_RGBA,
                     gl::GL_UNSIGNED_BYTE, rgba);
    gl::glBindTexture(gl::GL_TEXTURE_2D, 0);
    uiTextures_.push_back(tex);
    return reinterpret_cast<void*>(static_cast<intptr_t>(tex));
}

void OpenGLRenderer::destroyUiTexture(void* texture) {
    const auto tex = static_cast<gl::GLuint>(reinterpret_cast<intptr_t>(texture));
    if (!tex) return;
    // En GL la orden se encola detras de los dibujos ya emitidos: se puede borrar ya.
    gl::glDeleteTextures(1, &tex);
    std::erase(uiTextures_, tex);
}

void OpenGLRenderer::resizeIfNeeded(int fbWidth, int fbHeight) {
    width_ = fbWidth;
    height_ = fbHeight;
}

void OpenGLRenderer::setClearColor(float r, float g, float b, float a) {
    clearColor_[0] = r;
    clearColor_[1] = g;
    clearColor_[2] = b;
    clearColor_[3] = a;
}

void OpenGLRenderer::createGizmoBuffers() {
    auto setup = [](gl::GLuint& vao, gl::GLuint& vbo) {
        gl::glGenVertexArrays(1, &vao);
        gl::glBindVertexArray(vao);
        gl::glGenBuffers(1, &vbo);
        gl::glBindBuffer(gl::GL_ARRAY_BUFFER, vbo);
        gl::glBufferData(gl::GL_ARRAY_BUFFER, (gl::GLsizeiptr)(kGizmoMaxVertices * sizeof(Vertex)), nullptr,
                          gl::GL_DYNAMIC_DRAW);
        setupVertexAttribs();
    };
    setup(gizmoLineVao_, gizmoLineVbo_);
    setup(gizmoTriVao_, gizmoTriVbo_);
    setup(gridVao_, gridVbo_);
}

MeshHandle OpenGLRenderer::createMesh(const MeshData& data) {
    MeshResource res;
    gl::glGenVertexArrays(1, &res.vao);
    gl::glBindVertexArray(res.vao);

    gl::glGenBuffers(1, &res.vbo);
    gl::glBindBuffer(gl::GL_ARRAY_BUFFER, res.vbo);
    gl::glBufferData(gl::GL_ARRAY_BUFFER, (gl::GLsizeiptr)(data.vertices.size() * sizeof(Vertex)),
                      data.vertices.data(), gl::GL_STATIC_DRAW);

    gl::glGenBuffers(1, &res.ebo);
    gl::glBindBuffer(gl::GL_ELEMENT_ARRAY_BUFFER, res.ebo);
    gl::glBufferData(gl::GL_ELEMENT_ARRAY_BUFFER, (gl::GLsizeiptr)(data.indices.size() * sizeof(uint32_t)),
                      data.indices.data(), gl::GL_STATIC_DRAW);

    setupLitVertexAttribs();
    res.indexCount = (gl::GLsizei)data.indices.size();

    for (size_t i = 0; i < meshes_.size(); ++i) {
        if (meshes_[i].vao == 0) { // hueco libre de un destroyMesh() anterior
            meshes_[i] = res;
            return (MeshHandle)i;
        }
    }
    meshes_.push_back(res);
    return (MeshHandle)(meshes_.size() - 1);
}

void OpenGLRenderer::destroyMesh(MeshHandle handle) {
    if (handle >= meshes_.size()) return;
    MeshResource& r = meshes_[handle];
    if (r.vao == 0) return;
    gl::glDeleteVertexArrays(1, &r.vao);
    gl::glDeleteBuffers(1, &r.vbo);
    gl::glDeleteBuffers(1, &r.ebo);
    r = MeshResource{};
}

bool OpenGLRenderer::beginFrame() {
    if (width_ <= 0 || height_ <= 0) return false;
    gl::glViewport(0, 0, width_, height_);
    gl::glClearColor(clearColor_[0], clearColor_[1], clearColor_[2], clearColor_[3]);
    gl::glClear(gl::GL_COLOR_BUFFER_BIT | gl::GL_DEPTH_BUFFER_BIT);
    // GL cuenta Y desde abajo; el rectangulo llega con origen arriba-izquierda.
    if (vpW_ > 0 && vpH_ > 0) gl::glViewport(vpX_, height_ - (vpY_ + vpH_), vpW_, vpH_);
    return true;
}

// Sin sombras en OpenGL (ver ROADMAP_RENDER.md) -- beginFrame() ya deja GL
// listo para dibujar, asi que las cuatro son no-ops.
void OpenGLRenderer::beginShadowPass(const glm::mat4& /*lightViewProj*/) {}
void OpenGLRenderer::drawMeshShadow(MeshHandle /*mesh*/, const glm::mat4& /*model*/) {}
void OpenGLRenderer::endShadowPass() {}
// Sin SSAO en OpenGL (mismo criterio que las sombras arriba) -- no-ops.
void OpenGLRenderer::beginGBufferPass(const glm::mat4& /*view*/, const glm::mat4& /*proj*/) {}
void OpenGLRenderer::drawMeshGBuffer(MeshHandle /*mesh*/, const glm::mat4& /*model*/) {}
void OpenGLRenderer::endGBufferPass() {}
void OpenGLRenderer::beginMainPass() {}

void OpenGLRenderer::setViewportRect(int x, int y, int width, int height) {
    vpX_ = x < 0 ? 0 : x;
    vpY_ = y < 0 ? 0 : y;
    vpW_ = width < 0 ? 0 : width;
    vpH_ = height < 0 ? 0 : height;
}

void OpenGLRenderer::setDrawRect(int x, int y, int width, int height) {
    if (width <= 0 || height <= 0) return;
    // GL cuenta Y desde abajo; el rectangulo llega con origen arriba-izquierda.
    gl::glViewport(x < 0 ? 0 : x, height_ - (y + height), width, height);
}

void OpenGLRenderer::drawGrid(const std::vector<Vertex>& lineVerts, const glm::mat4& viewProj) {
    if (lineVerts.empty()) return;
    gl::glDisable(gl::GL_DEPTH_TEST);
    gl::glUseProgram(program_);
    gl::glUniformMatrix4fv(uMvpLocation_, 1, gl::GL_FALSE_, &viewProj[0][0]);
    const size_t n = std::min(lineVerts.size(), (size_t)kGizmoMaxVertices);
    gl::glBindVertexArray(gridVao_);
    gl::glBindBuffer(gl::GL_ARRAY_BUFFER, gridVbo_);
    gl::glBufferSubData(gl::GL_ARRAY_BUFFER, 0, (gl::GLsizeiptr)(n * sizeof(Vertex)), lineVerts.data());
    gl::glDrawArrays(gl::GL_LINES, 0, (gl::GLsizei)n);
    gl::glEnable(gl::GL_DEPTH_TEST);
}

void OpenGLRenderer::setLighting(const glm::vec3& lightPosWS, const glm::vec3& eyePosWS,
                                  const glm::mat4& /*lightViewProj*/) {
    lightPosWS_ = lightPosWS;
    eyePosWS_ = eyePosWS;
}

void OpenGLRenderer::drawMesh(MeshHandle mesh, const glm::mat4& model, const glm::mat4& mvp,
                               const Material& material, const TexMapping& tex) {
    if (mesh >= meshes_.size() || meshes_[mesh].vao == 0) return;
    gl::glUseProgram(litProgram_);
    gl::GLuint colorTex = whiteTexture_;
    int texFlags = 0;
    glm::vec4 uvTransform(1.0f, 1.0f, 0.0f, 0.0f);
    if (tex.enabled) {
        if (gl::GLuint t = materialTexture(material.texture(TexSlot::Color), material.texBaseDir)) {
            colorTex = t;
            texFlags = 1 | (tex.tile ? 2 : 0);
            uvTransform = texMappingUvTransform(tex);
        }
    }
    gl::glActiveTexture(gl::GL_TEXTURE0);
    gl::glBindTexture(gl::GL_TEXTURE_2D, colorTex);
    gl::glUniform1i(uLitColorTexLoc_, 0);
    gl::glUniform1i(uLitTexFlagsLoc_, texFlags);
    gl::glUniform4fv(uLitUvTransformLoc_, 1, &uvTransform.x);
    gl::glUniformMatrix4fv(uLitModelLoc_, 1, gl::GL_FALSE_, &model[0][0]);
    gl::glUniformMatrix4fv(uLitMvpLoc_, 1, gl::GL_FALSE_, &mvp[0][0]);
    gl::glUniform3fv(uLitLightPosLoc_, 1, &lightPosWS_.x);
    gl::glUniform3fv(uLitEyePosLoc_, 1, &eyePosWS_.x);
    // Backend OpenGL = Phong de siempre, sin subir a PBR (decision de Core,
    // 2026-10-30, ver ROADMAP_RENDER.md: el esfuerzo real de render va a
    // Vulkan). Aqui solo se aproxima specular/shininess a partir de
    // metallic/roughness para que el material PBR se vea razonable tambien
    // en este backend, no para que sea fisicamente correcto.
    const float specularIntensity = glm::mix(0.2f, 1.0f, material.metallic);
    const float shininess = glm::mix(96.0f, 4.0f, material.roughness);
    gl::glUniform3fv(uLitDiffuseLoc_, 1, &material.baseColor.x);
    gl::glUniform1f(uLitSpecularLoc_, specularIntensity);
    gl::glUniform1f(uLitShininessLoc_, shininess);
    gl::glBindVertexArray(meshes_[mesh].vao);
    gl::glDrawElements(gl::GL_TRIANGLES, meshes_[mesh].indexCount, gl::GL_UNSIGNED_INT, nullptr);
}

gl::GLuint OpenGLRenderer::materialTexture(const std::string& rawPath, const std::string& baseDir) {
    if (rawPath.empty()) return 0;
    const std::string key = textureCacheKey(rawPath, baseDir);
    auto it = materialTextures_.find(key);
    if (it != materialTextures_.end()) return it->second;
    gl::GLuint tex = 0;
    const std::string path = resolveTexturePath(rawPath, baseDir);
    TextureImage img;
    if (path.empty())
        std::fprintf(stderr, "[Iron3D] Textura no encontrada: %s\n", rawPath.c_str());
    else if (!loadTextureImage(path, img))
        std::fprintf(stderr, "[Iron3D] Formato de textura no soportado o archivo roto: %s\n", path.c_str());
    else
        tex = createRepeatTexture(img.rgba.data(), img.width, img.height);
    materialTextures_.emplace(key, tex);
    return tex;
}

void OpenGLRenderer::drawGizmo(const std::vector<Vertex>& lineVerts, const std::vector<Vertex>& triVerts,
                                const glm::mat4& viewProj) {
    // El gizmo se dibuja SIEMPRE encima de la escena, sin que la geometria lo
    // pueda tapar -- igual que los handles de transformacion de cualquier DCC
    // (C4D incluido). Con el buffer de profundidad ya activo para drawMesh(),
    // hay que desactivarlo aqui a proposito; se reactiva al final para que el
    // siguiente beginFrame()/drawMesh() lo encuentre como lo dejo init().
    gl::glDisable(gl::GL_DEPTH_TEST);
    gl::glUseProgram(program_);
    gl::glUniformMatrix4fv(uMvpLocation_, 1, gl::GL_FALSE_, &viewProj[0][0]); // vertices ya en espacio de mundo

    if (!triVerts.empty()) {
        const size_t n = std::min(triVerts.size(), (size_t)kGizmoMaxVertices);
        gl::glBindVertexArray(gizmoTriVao_);
        gl::glBindBuffer(gl::GL_ARRAY_BUFFER, gizmoTriVbo_);
        gl::glBufferSubData(gl::GL_ARRAY_BUFFER, 0, (gl::GLsizeiptr)(n * sizeof(Vertex)), triVerts.data());
        gl::glDrawArrays(gl::GL_TRIANGLES, 0, (gl::GLsizei)n);
    }
    if (!lineVerts.empty()) {
        const size_t n = std::min(lineVerts.size(), (size_t)kGizmoMaxVertices);
        gl::glBindVertexArray(gizmoLineVao_);
        gl::glBindBuffer(gl::GL_ARRAY_BUFFER, gizmoLineVbo_);
        gl::glBufferSubData(gl::GL_ARRAY_BUFFER, 0, (gl::GLsizeiptr)(n * sizeof(Vertex)), lineVerts.data());
        gl::glDrawArrays(gl::GL_LINES, 0, (gl::GLsizei)n);
    }

    gl::glEnable(gl::GL_DEPTH_TEST);
}

void OpenGLRenderer::endFrame() {
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    glfwSwapBuffers(window_);
}

void OpenGLRenderer::imguiNewFrame() {
    ImGui_ImplOpenGL3_NewFrame();
}

} // namespace iron3d
