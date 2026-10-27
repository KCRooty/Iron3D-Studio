#include "SceneNode.h"

#include <algorithm>

namespace iron3d {

namespace {
int nextId() {
    static int counter = 0;
    return counter++;
}
} // namespace

SceneNode::SceneNode(std::string name, bool hasMesh_)
    : hasMesh(hasMesh_), id_(nextId()), name_(std::move(name)) {}

SceneNode* SceneNode::addChild(std::string name, bool hasMesh_) {
    auto child = std::make_unique<SceneNode>(std::move(name), hasMesh_);
    child->parent_ = this;
    children_.push_back(std::move(child));
    return children_.back().get();
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

} // namespace iron3d
