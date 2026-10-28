#include "SceneNode.h"

#include <algorithm>

namespace iron3d {

namespace {
int nextId() {
    static int counter = 0;
    return counter++;
}
} // namespace

const char* primitiveTypeName(PrimitiveType type) {
    switch (type) {
        case PrimitiveType::Cube: return "Cubo";
        case PrimitiveType::Sphere: return "Esfera";
        case PrimitiveType::Plane: return "Plano";
        case PrimitiveType::Light: return "Luz";
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

bool SceneNode::removeChild(SceneNode* child) {
    auto it = std::find_if(children_.begin(), children_.end(),
                            [child](const std::unique_ptr<SceneNode>& n) { return n.get() == child; });
    if (it == children_.end()) return false;
    children_.erase(it);
    return true;
}

glm::mat4 SceneNode::localMatrix() const {
    glm::mat4 t = glm::translate(glm::mat4(1.0f), position);
    glm::mat4 rx = glm::rotate(glm::mat4(1.0f), glm::radians(rotationDeg.x), glm::vec3(1.0f, 0.0f, 0.0f));
    glm::mat4 ry = glm::rotate(glm::mat4(1.0f), glm::radians(rotationDeg.y), glm::vec3(0.0f, 1.0f, 0.0f));
    glm::mat4 rz = glm::rotate(glm::mat4(1.0f), glm::radians(rotationDeg.z), glm::vec3(0.0f, 0.0f, 1.0f));
    glm::mat4 s = glm::scale(glm::mat4(1.0f), scale);
    return t * rz * ry * rx * s;
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
    copy->visible = src.visible;
    for (const auto& child : src.children()) cloneInto(*copy, *child, child->name());
    return copy;
}
} // namespace

SceneNode* duplicateSubtree(SceneNode& parent, const SceneNode& src) {
    return cloneInto(parent, src, src.name() + " copia");
}

} // namespace iron3d
