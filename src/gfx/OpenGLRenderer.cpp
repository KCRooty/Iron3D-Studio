#include "OpenGLRenderer.h"

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
uniform mat4 uModel;
uniform mat4 uMvp;
out vec3 fragNormalWS;
out vec3 fragPosWS;
void main() {
    vec4 worldPos = uModel * vec4(inPosition, 1.0);
    gl_Position = uMvp * vec4(inPosition, 1.0);
    fragNormalWS = normalize(mat3(uModel) * inNormal);
    fragPosWS = worldPos.xyz;
}
)GLSL";

const char* kLitFragmentSrc = R"GLSL(#version 330 core
in vec3 fragNormalWS;
in vec3 fragPosWS;
uniform vec3 uLightPos;
uniform vec3 uEyePos;
uniform vec3 uMaterialDiffuse;
uniform float uMaterialSpecular;
uniform float uMaterialShininess;
out vec4 outColor;
void main() {
    vec3 N = normalize(fragNormalWS);
    vec3 L = normalize(uLightPos - fragPosWS);
    vec3 V = normalize(uEyePos - fragPosWS);
    vec3 R = reflect(-L, N);
    const vec3 lightColor = vec3(1.0);

    vec3 ambient = 0.15 * uMaterialDiffuse;
    float diff = max(dot(N, L), 0.0);
    vec3 diffuse = diff * uMaterialDiffuse * lightColor;
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
    gl::glDeleteProgram(program_);
    gl::glDeleteProgram(litProgram_);

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
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
    gl::glBufferData(gl::GL_ELEMENT_ARRAY_BUFFER, (gl::GLsizeiptr)(data.indices.size() * sizeof(uint16_t)),
                      data.indices.data(), gl::GL_STATIC_DRAW);

    setupLitVertexAttribs();
    res.indexCount = (gl::GLsizei)data.indices.size();

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
    gl::glClear(gl::GL_COLOR_BUFFER_BIT);
    return true;
}

void OpenGLRenderer::setLighting(const glm::vec3& lightPosWS, const glm::vec3& eyePosWS) {
    lightPosWS_ = lightPosWS;
    eyePosWS_ = eyePosWS;
}

void OpenGLRenderer::drawMesh(MeshHandle mesh, const glm::mat4& model, const glm::mat4& mvp,
                               const Material& material) {
    if (mesh >= meshes_.size() || meshes_[mesh].vao == 0) return;
    gl::glUseProgram(litProgram_);
    gl::glUniformMatrix4fv(uLitModelLoc_, 1, gl::GL_FALSE_, &model[0][0]);
    gl::glUniformMatrix4fv(uLitMvpLoc_, 1, gl::GL_FALSE_, &mvp[0][0]);
    gl::glUniform3fv(uLitLightPosLoc_, 1, &lightPosWS_.x);
    gl::glUniform3fv(uLitEyePosLoc_, 1, &eyePosWS_.x);
    gl::glUniform3fv(uLitDiffuseLoc_, 1, &material.diffuse.x);
    gl::glUniform1f(uLitSpecularLoc_, material.specular);
    gl::glUniform1f(uLitShininessLoc_, material.shininess);
    gl::glBindVertexArray(meshes_[mesh].vao);
    gl::glDrawElements(gl::GL_TRIANGLES, meshes_[mesh].indexCount, gl::GL_UNSIGNED_SHORT, nullptr);
}

void OpenGLRenderer::drawGizmo(const std::vector<Vertex>& lineVerts, const std::vector<Vertex>& triVerts,
                                const glm::mat4& viewProj) {
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
}

void OpenGLRenderer::endFrame() {
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    glfwSwapBuffers(window_);
}

void OpenGLRenderer::imguiNewFrame() {
    ImGui_ImplOpenGL3_NewFrame();
}

} // namespace iron3d
