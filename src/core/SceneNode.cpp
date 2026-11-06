#include "SceneNode.h"

#include <algorithm>
#include <cmath>

namespace iron3d {

namespace {
int nextId() {
    static int counter = 0;
    return counter++;
}
} // namespace

glm::mat3 hpbToMat3(const glm::vec3& deg) {
    const glm::mat4 ry = glm::rotate(glm::mat4(1.0f), glm::radians(deg.y), glm::vec3(0.0f, 1.0f, 0.0f));
    const glm::mat4 rx = glm::rotate(glm::mat4(1.0f), glm::radians(deg.x), glm::vec3(1.0f, 0.0f, 0.0f));
    const glm::mat4 rz = glm::rotate(glm::mat4(1.0f), glm::radians(deg.z), glm::vec3(0.0f, 0.0f, 1.0f));
    return glm::mat3(ry * rx * rz);
}

glm::vec3 mat3ToHpb(const glm::mat3& m) {
    // R = Ry(h) Rx(p) Rz(b). glm indexa m[columna][fila]:
    //   fila 1 = (cp*sb, cp*cb, -sp); columna 2 = (sh*cp, -sp, ch*cp).
    const float sp = std::clamp(-m[2][1], -1.0f, 1.0f);
    const float p = std::asin(sp);
    float h, b;
    if (std::abs(sp) < 0.9999f) {
        h = std::atan2(m[2][0], m[2][2]);
        b = std::atan2(m[0][1], m[1][1]);
    } else {  // bloqueo de cardan (P = +-90): el Banco se fija a 0
        b = 0.0f;
        h = std::atan2(-m[0][2], m[0][0]);
    }
    glm::vec3 deg = glm::degrees(glm::vec3(p, h, b));
    // Sin "-0.0" ni restos de redondeo en el Gestor de Coordenadas.
    for (int i = 0; i < 3; ++i)
        if (std::abs(deg[i]) < 1e-4f) deg[i] = 0.0f;
    return deg;
}

glm::mat3 legacyXyzToMat3(const glm::vec3& deg) {
    const glm::mat4 rx = glm::rotate(glm::mat4(1.0f), glm::radians(deg.x), glm::vec3(1.0f, 0.0f, 0.0f));
    const glm::mat4 ry = glm::rotate(glm::mat4(1.0f), glm::radians(deg.y), glm::vec3(0.0f, 1.0f, 0.0f));
    const glm::mat4 rz = glm::rotate(glm::mat4(1.0f), glm::radians(deg.z), glm::vec3(0.0f, 0.0f, 1.0f));
    return glm::mat3(rz * ry * rx);
}

const char* primitiveTypeName(PrimitiveType type) {
    switch (type) {
        case PrimitiveType::Cube: return "Cubo";
        case PrimitiveType::Sphere: return "Esfera";
        case PrimitiveType::Plane: return "Plano";
        case PrimitiveType::Light: return "Luz";
        case PrimitiveType::Mesh: return "Objeto poligonal";
        case PrimitiveType::None: default: return "Nulo (vacio)";
    }
}

SceneNode::SceneNode(std::string name, PrimitiveType primitive_)
    : primitive(primitive_), id_(nextId()), name_(std::move(name)) {}

SceneNode* SceneNode::addChild(std::string name, PrimitiveType primitive_) {
    auto child = std::make_unique<SceneNode>(std::move(name), primitive_);
    child->parent_ = this;
    children_.push_back(std::move(child));
    return children_.back().get();
}

void SceneNode::adoptChildrenFrom(SceneNode& other) {
    children_ = std::move(other.children_);
    for (auto& child : children_) child->parent_ = this;
    other.children_.clear();
}

void SceneNode::appendChildrenFrom(SceneNode& other) {
    for (auto& child : other.children_) {
        child->parent_ = this;
        children_.push_back(std::move(child));
    }
    other.children_.clear();
}

bool SceneNode::removeChild(SceneNode* child) {
    auto it = std::find_if(children_.begin(), children_.end(),
                            [child](const std::unique_ptr<SceneNode>& n) { return n.get() == child; });
    if (it == children_.end()) return false;
    children_.erase(it);
    return true;
}

std::unique_ptr<SceneNode> SceneNode::detachChild(SceneNode* child) {
    auto it = std::find_if(children_.begin(), children_.end(),
                            [child](const std::unique_ptr<SceneNode>& n) { return n.get() == child; });
    if (it == children_.end()) return nullptr;
    std::unique_ptr<SceneNode> out = std::move(*it);
    children_.erase(it);
    out->parent_ = nullptr;
    return out;
}

SceneNode* SceneNode::insertChild(std::unique_ptr<SceneNode> child, size_t index) {
    if (!child) return nullptr;
    child->parent_ = this;
    index = std::min(index, children_.size());
    auto it = children_.insert(children_.begin() + (std::ptrdiff_t)index, std::move(child));
    return it->get();
}

size_t SceneNode::indexOf(const SceneNode* child) const {
    for (size_t i = 0; i < children_.size(); ++i)
        if (children_[i].get() == child) return i;
    return children_.size();
}

void SceneNode::setLocalMatrix(const glm::mat4& m) {
    position = glm::vec3(m[3]);
    glm::vec3 c0(m[0]), c1(m[1]), c2(m[2]);
    glm::vec3 s(glm::length(c0), glm::length(c1), glm::length(c2));
    // Matriz con reflejo: se lo lleva la escala X.
    if (glm::dot(glm::cross(c0, c1), c2) < 0.0f) s.x = -s.x;
    if (s.x != 0.0f) c0 /= s.x;
    if (s.y != 0.0f) c1 /= s.y;
    if (s.z != 0.0f) c2 /= s.z;
    scale = s;
    rotationDeg = mat3ToHpb(glm::mat3(c0, c1, c2));
}

glm::mat4 SceneNode::localMatrix() const {
    const glm::mat4 t = glm::translate(glm::mat4(1.0f), position);
    const glm::mat4 r(hpbToMat3(rotationDeg));
    const glm::mat4 s = glm::scale(glm::mat4(1.0f), scale);
    return t * r * s;
}

glm::mat4 SceneNode::parentWorldMatrix() const {
    return parent_ ? parent_->worldMatrix() : glm::mat4(1.0f);
}

glm::mat4 SceneNode::worldMatrix() const {
    return parentWorldMatrix() * localMatrix();
}

namespace {
SceneNode* cloneInto(SceneNode& parent, const SceneNode& src, std::string name) {
    SceneNode* copy = parent.addChild(std::move(name), src.primitive);
    copy->position = src.position;
    copy->rotationDeg = src.rotationDeg;
    copy->scale = src.scale;
    copy->editorVis = src.editorVis;
    copy->renderVis = src.renderVis;
    copy->materialIndex = src.materialIndex;
    copy->mesh = src.mesh;
    for (const auto& child : src.children()) cloneInto(*copy, *child, child->name());
    return copy;
}
} // namespace

SceneNode* duplicateSubtree(SceneNode& parent, const SceneNode& src) {
    return cloneInto(parent, src, src.name() + " copia");
}

} // namespace iron3d
