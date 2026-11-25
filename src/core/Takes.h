#pragma once

// Tomas (2026-11-25): como el Gestor de Tomas de C4D, pero con un alcance concreto. La toma Principal (indice 0)
// es la escena base; cada toma extra guarda "anulaciones" de un nodo: visibilidad en editor y render, posicion,
// rotacion y escala. Al cambiar de toma, la escena vuelve a los valores base y se aplican los de la toma elegida.
// Todo vive en el nodo raiz (SceneNode::takes / activeTake) y se guarda con la escena. Solo cabecera.

#include "SceneNode.h"

#include <algorithm>
#include <sstream>
#include <string>
#include <vector>

namespace iron3d {
namespace takes {

inline std::string pathOf(const SceneNode& root, const SceneNode& n) {
    std::vector<std::string> parts;
    for (const SceneNode* c = &n; c && c != &root; c = c->parent()) parts.push_back(c->name());
    std::string out;
    for (size_t i = parts.size(); i-- > 0;) out += (out.empty() ? "" : "/") + parts[i];
    return out;
}

inline SceneNode* find(SceneNode& root, const std::string& path) {
    SceneNode* cur = &root;
    std::stringstream ss(path);
    std::string part;
    while (std::getline(ss, part, '/')) {
        SceneNode* next = nullptr;
        for (const auto& c : cur->children())
            if (c->name() == part) { next = c.get(); break; }
        if (!next) return nullptr;
        cur = next;
    }
    return cur == &root && !path.empty() ? nullptr : cur;
}

inline TakeState stateOf(const SceneNode& n) {
    return {(int)n.editorVis, (int)n.renderVis, n.position, n.rotationDeg, n.scale};
}

inline void applyState(SceneNode& n, const TakeState& s) {
    n.editorVis = (VisMode)std::clamp(s.editorVis, 0, 2);
    n.renderVis = (VisMode)std::clamp(s.renderVis, 0, 2);
    n.position = s.position;
    n.rotationDeg = s.rotationDeg;
    n.scale = s.scale;
}

inline void ensure(SceneNode& root) {
    if (root.takes.empty()) {
        Take main;
        main.name = "Principal";
        root.takes.push_back(main);
        root.activeTake = 0;
    }
}

// Guarda lo que hay en pantalla en la toma activa: valores base si es la Principal, anulaciones si es otra.
inline void captureLive(SceneNode& root) {
    if (root.takes.empty()) return;
    const int act = std::clamp(root.activeTake, 0, (int)root.takes.size() - 1);
    if (act == 0) {
        for (size_t t = 1; t < root.takes.size(); ++t)
            for (TakeOverride& o : root.takes[t].overrides)
                if (SceneNode* n = find(root, o.path)) o.base = stateOf(*n);
    } else {
        for (TakeOverride& o : root.takes[(size_t)act].overrides)
            if (SceneNode* n = find(root, o.path)) o.value = stateOf(*n);
    }
}

inline void activate(SceneNode& root, int idx) {
    ensure(root);
    idx = std::clamp(idx, 0, (int)root.takes.size() - 1);
    captureLive(root);
    // Vuelta a la base de todos los nodos con anulaciones, y luego las de la toma elegida.
    for (size_t t = 1; t < root.takes.size(); ++t)
        for (const TakeOverride& o : root.takes[t].overrides)
            if (SceneNode* n = find(root, o.path)) applyState(*n, o.base);
    if (idx > 0)
        for (const TakeOverride& o : root.takes[(size_t)idx].overrides)
            if (SceneNode* n = find(root, o.path)) applyState(*n, o.value);
    root.activeTake = idx;
}

inline int add(SceneNode& root, const std::string& name) {
    ensure(root);
    Take t;
    t.name = name;
    root.takes.push_back(t);
    return (int)root.takes.size() - 1;
}

// Anade el nodo a la toma activa (no a la Principal): desde ahora sus cambios se guardan en esa toma.
inline bool addOverride(SceneNode& root, const SceneNode& node) {
    ensure(root);
    const int act = root.activeTake;
    if (act <= 0 || act >= (int)root.takes.size() || &node == &root) return false;
    const std::string path = pathOf(root, node);
    Take& t = root.takes[(size_t)act];
    for (const TakeOverride& o : t.overrides)
        if (o.path == path) return false;
    // La base es lo que valia en la Principal: si otra toma ya lo anulaba, su base.
    TakeState base = stateOf(node);
    for (size_t k = 1; k < root.takes.size(); ++k)
        for (const TakeOverride& o : root.takes[k].overrides)
            if (o.path == path) base = o.base;
    t.overrides.push_back({path, base, stateOf(node)});
    return true;
}

inline void removeNodeFromAll(SceneNode& root, const SceneNode& node) {
    const std::string path = pathOf(root, node);
    const int act = root.activeTake;
    for (size_t t = 1; t < root.takes.size(); ++t) {
        auto& ov = root.takes[t].overrides;
        for (auto it = ov.begin(); it != ov.end();) {
            if (it->path == path) {
                if ((int)t == act) applyState(const_cast<SceneNode&>(node), it->base);
                it = ov.erase(it);
            } else ++it;
        }
    }
}

inline void remove(SceneNode& root, int idx) {
    if (idx <= 0 || idx >= (int)root.takes.size()) return;
    if (root.activeTake == idx) activate(root, 0);
    root.takes.erase(root.takes.begin() + idx);
    if (root.activeTake > idx) --root.activeTake;
}

} // namespace takes
} // namespace iron3d
