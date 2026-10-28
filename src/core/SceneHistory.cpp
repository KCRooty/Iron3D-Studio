#include "SceneHistory.h"
#include "SceneSerializer.h"

namespace iron3d {

std::string SceneHistory::snapshot(const SceneNode& root) const {
    return serializeScene(root, materials_);
}

void SceneHistory::commit(std::string preChangeSnapshot) {
    undoStack_.push_back(std::move(preChangeSnapshot));
    if (undoStack_.size() > kMaxDepth) undoStack_.erase(undoStack_.begin());
    redoStack_.clear();
}

bool SceneHistory::undo(SceneNode& root) {
    if (undoStack_.empty()) return false;

    redoStack_.push_back(snapshot(root)); // para poder volver aqui con un rehacer despues
    if (redoStack_.size() > kMaxDepth) redoStack_.erase(redoStack_.begin());

    const std::string prev = std::move(undoStack_.back());
    undoStack_.pop_back();
    std::string err;
    return deserializeScene(prev, root, materials_, &err);
}

bool SceneHistory::redo(SceneNode& root) {
    if (redoStack_.empty()) return false;

    undoStack_.push_back(snapshot(root));
    if (undoStack_.size() > kMaxDepth) undoStack_.erase(undoStack_.begin());

    const std::string next = std::move(redoStack_.back());
    redoStack_.pop_back();
    std::string err;
    return deserializeScene(next, root, materials_, &err);
}

} // namespace iron3d
