#include "SceneNode.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

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
        case PrimitiveType::Camera: return "Cámara";
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

bool SceneNode::canReparentNodes(const std::vector<SceneNode*>& nodes, const SceneNode& newParent) {
    if (nodes.empty()) return false;
    for (const SceneNode* n : nodes) {
        if (!n || !n->parent_) return false;
        for (const SceneNode* p = &newParent; p; p = p->parent_)
            if (p == n) return false;
    }
    return true;
}

bool SceneNode::reparentNodes(const std::vector<SceneNode*>& nodes, SceneNode& newParent, size_t index) {
    if (!canReparentNodes(nodes, newParent)) return false;
    index = std::min(index, newParent.children_.size());
    for (SceneNode* n : nodes) {
        SceneNode* old = n->parent_;
        if (old == &newParent && old->indexOf(n) < index) --index; // al sacarlo, lo de detras sube un puesto
        const glm::mat4 world = n->worldMatrix();
        SceneNode* moved = newParent.insertChild(old->detachChild(n), index);
        moved->setLocalMatrix(glm::inverse(newParent.worldMatrix()) * world);
        ++index;
    }
    return true;
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
    copy->light = src.light;
    copy->camera = src.camera;
    copy->texTag = src.texTag;
    copy->isJoint = src.isJoint;
    copy->jointLimits = src.jointLimits;
    copy->protection = src.protection;
    copy->nullDisplay = src.nullDisplay;
    copy->useColor = src.useColor;
    copy->displayColor = src.displayColor;
    copy->xray = src.xray;
    copy->phongLimit = src.phongLimit;
    copy->phongAngle = src.phongAngle;
    copy->ik = src.ik;
    copy->param = src.param;
    copy->deform.present = src.deform.present;
    copy->deform.kind = src.deform.kind;
    copy->deform.axis = src.deform.axis;
    copy->deform.amount = src.deform.amount;
    for (int i = 0; i < 8; ++i) copy->deform.ffd[i] = src.deform.ffd[i];
    copy->deform.param2 = src.deform.param2;
    copy->deform.formula = src.deform.formula;
    copy->deform.bindRest = src.deform.bindRest;
    copy->instance = src.instance;
    copy->env = src.env;
    copy->spline = src.spline;
    {
        const CloneData& s = src.clone;
        CloneData& d = copy->clone;
        d.present = s.present; d.mode = s.mode; d.count = s.count; d.countY = s.countY; d.countZ = s.countZ;
        d.offset = s.offset; d.radiusCm = s.radiusCm; d.axis = s.axis; d.scaleStepPct = s.scaleStepPct; d.rotStepDeg = s.rotStepDeg;
        d.randPosCm = s.randPosCm; d.randRotDeg = s.randRotDeg; d.randScalePct = s.randScalePct; d.seed = s.seed;
    }
    copy->symmetry.present = src.symmetry.present;  // la cache se rehace sola
    copy->symmetry.axis = src.symmetry.axis;
    copy->symmetry.tolCm = src.symmetry.tolCm;
    copy->materialParts = src.materialParts;
    copy->skin = src.skin;
    copy->animPos = src.animPos;
    copy->animRot = src.animRot;
    copy->animScale = src.animScale;
    copy->animParams = src.animParams;
    copy->animPla = src.animPla;
    for (const auto& child : src.children()) cloneInto(*copy, *child, child->name());
    return copy;
}
} // namespace

void AnimTrack::set(int frame, const glm::vec3& value) {
    auto it = std::lower_bound(keys.begin(), keys.end(), frame, [](const AnimKey& k, int f) { return k.frame < f; });
    if (it != keys.end() && it->frame == frame) it->value = value;
    else keys.insert(it, AnimKey{frame, value, KeyInterp::Spline});
}

bool AnimTrack::remove(int frame) {
    auto it = std::find_if(keys.begin(), keys.end(), [&](const AnimKey& k) { return k.frame == frame; });
    if (it == keys.end()) return false;
    keys.erase(it);
    return true;
}

bool AnimTrack::hasKeyAt(int frame) const {
    return std::any_of(keys.begin(), keys.end(), [&](const AnimKey& k) { return k.frame == frame; });
}

namespace {
// Pendiente automatica (valor por fotograma) de la clave i, por componente:
// la de la recta entre sus vecinas, o 0 en los extremos y donde la clave es
// un maximo/minimo local (asi la curva nunca se pasa de las claves).
glm::vec3 autoSlope(const std::vector<AnimKey>& k, size_t i) {
    if (i == 0 || i + 1 >= k.size()) return glm::vec3(0.0f);
    const float df = (float)(k[i + 1].frame - k[i - 1].frame);
    glm::vec3 s = (k[i + 1].value - k[i - 1].value) / std::max(df, 1e-6f);
    for (int c = 0; c < 3; ++c) {
        const float a = k[i].value[c] - k[i - 1].value[c], b = k[i + 1].value[c] - k[i].value[c];
        if (a * b <= 0.0f) s[c] = 0.0f;
    }
    return s;
}
} // namespace

glm::vec3 AnimTrack::evaluate(float frame) const {
    if (frame <= (float)keys.front().frame) return keys.front().value;
    if (frame >= (float)keys.back().frame) return keys.back().value;
    size_t i = 0;
    while (i + 1 < keys.size() && (float)keys[i + 1].frame <= frame) ++i;
    const AnimKey& a = keys[i];
    const AnimKey& b = keys[i + 1];
    const float span = (float)(b.frame - a.frame);
    const float t = (frame - (float)a.frame) / span;
    if (a.interp == KeyInterp::Step) return a.value;
    if (a.interp == KeyInterp::Linear) return a.value + (b.value - a.value) * t;
    const glm::vec3 m0 = autoSlope(keys, i) * span, m1 = autoSlope(keys, i + 1) * span;
    const float t2 = t * t, t3 = t2 * t;
    return (2 * t3 - 3 * t2 + 1) * a.value + (t3 - 2 * t2 + t) * m0 + (-2 * t3 + 3 * t2) * b.value + (t3 - t2) * m1;
}

std::vector<std::pair<std::string, const char*>> animParamIds(const SceneNode& node) {
    std::vector<std::pair<std::string, const char*>> out;
    if (node.deform.present) {
        out.push_back({"def.amount", "Fuerza"});
        out.push_back({"def.param2", "Parámetro 2"});
    }
    if (node.spline.present && !node.spline.generated) {
        out.push_back({"spl.a", "Spline A"});
        out.push_back({"spl.b", "Spline B"});
        out.push_back({"spl.c", "Spline C"});
        out.push_back({"spl.start", "Ángulo Inicial"});
        out.push_back({"spl.end", "Ángulo Final"});
    }
    if (node.primitive == PrimitiveType::Light) {
        out.push_back({"light.intensity", "Intensidad"});
        out.push_back({"light.outer", "Ángulo Exterior"});
        out.push_back({"light.r", "Color R"});
        out.push_back({"light.g", "Color G"});
        out.push_back({"light.b", "Color B"});
    }
    if (node.primitive == PrimitiveType::Camera) out.push_back({"cam.focal", "Distancia Focal"});
    if (node.env.present) out.push_back({"env.level", "Nivel"});
    return out;
}

float* animParamPtr(SceneNode& n, const std::string& id) {
    if (id == "def.amount") return &n.deform.amount;
    if (id == "def.param2") return &n.deform.param2;
    if (id == "spl.a") return &n.spline.a;
    if (id == "spl.b") return &n.spline.b;
    if (id == "spl.c") return &n.spline.c;
    if (id == "spl.start") return &n.spline.startDeg;
    if (id == "spl.end") return &n.spline.endDeg;
    if (id == "light.intensity") return &n.light.intensity;
    if (id == "light.outer") return &n.light.outerAngleDeg;
    if (id == "light.r") return &n.light.color.r;
    if (id == "light.g") return &n.light.color.g;
    if (id == "light.b") return &n.light.color.b;
    if (id == "cam.focal") return &n.camera.focalMm;
    if (id == "env.level") return &n.env.level;
    return nullptr;
}

int recordParamKeys(SceneNode& node, int frame, bool onlyExisting) {
    int n = 0;
    for (const auto& [id, label] : animParamIds(node)) {
        (void)label;
        float* v = animParamPtr(node, id);
        if (!v) continue;
        auto it = std::find_if(node.animParams.begin(), node.animParams.end(), [&](const ParamTrack& t) { return t.id == id; });
        if (it == node.animParams.end()) {
            if (onlyExisting) continue;
            node.animParams.push_back({id, {}});
            it = node.animParams.end() - 1;
        }
        it->track.set(frame, glm::vec3(*v, 0.0f, 0.0f));
        ++n;
    }
    return n;
}

bool recordPlaKey(SceneNode& node, int frame) {
    if (node.primitive != PrimitiveType::Mesh || !node.mesh || node.mesh->vertices.empty()) return false;
    PlaKey k;
    k.frame = frame;
    for (const Vertex& v : node.mesh->vertices) k.positions.push_back(v.position);
    auto it = std::find_if(node.animPla.begin(), node.animPla.end(), [&](const PlaKey& p) { return p.frame == frame; });
    if (it != node.animPla.end()) {
        k.interp = it->interp;
        *it = std::move(k);
    } else {
        node.animPla.push_back(std::move(k));
        std::sort(node.animPla.begin(), node.animPla.end(), [](const PlaKey& a, const PlaKey& b) { return a.frame < b.frame; });
    }
    return true;
}

namespace {
void applyPla(SceneNode& node, float frame) {
    if (node.animPla.empty() || !node.mesh) return;
    const size_t nv = node.mesh->vertices.size();
    const auto& K = node.animPla;
    const PlaKey* a = &K.front();
    const PlaKey* b = &K.front();
    float t = 0.0f;
    if (frame <= K.front().frame) a = b = &K.front();
    else if (frame >= K.back().frame) a = b = &K.back();
    else
        for (size_t i = 0; i + 1 < K.size(); ++i)
            if (frame >= K[i].frame && frame <= K[i + 1].frame) {
                a = &K[i];
                b = &K[i + 1];
                t = a->interp == KeyInterp::Step ? 0.0f : (frame - a->frame) / (float)std::max(1, b->frame - a->frame);
                break;
            }
    if (a->positions.size() != nv || b->positions.size() != nv) return;
    bool same = true;
    for (size_t i = 0; i < nv && same; ++i)
        same = node.mesh->vertices[i].position == glm::mix(a->positions[i], b->positions[i], t);
    if (same) return;
    MeshData m = *node.mesh;
    for (size_t i = 0; i < nv; ++i) m.vertices[i].position = glm::mix(a->positions[i], b->positions[i], t);
    // normales por posicion (las caras comparten vertices solo a veces)
    for (Vertex& v : m.vertices) v.normal = glm::vec3(0.0f);
    for (size_t i = 0; i + 2 < m.indices.size(); i += 3) {
        Vertex &p0 = m.vertices[m.indices[i]], &p1 = m.vertices[m.indices[i + 1]], &p2 = m.vertices[m.indices[i + 2]];
        const glm::vec3 fn = glm::cross(p1.position - p0.position, p2.position - p0.position);
        p0.normal += fn;
        p1.normal += fn;
        p2.normal += fn;
    }
    for (Vertex& v : m.vertices) v.normal = glm::length(v.normal) > 1e-20f ? glm::normalize(v.normal) : glm::vec3(0, 1, 0);
    updateBounds(m);
    node.mesh = std::make_shared<const MeshData>(std::move(m));
}
} // namespace

void evaluateAnimation(SceneNode& node, float frame) {
    if (!node.animPos.empty()) node.position = node.animPos.evaluate(frame);
    if (!node.animRot.empty()) node.rotationDeg = node.animRot.evaluate(frame);
    if (!node.animScale.empty()) node.scale = node.animScale.evaluate(frame);
    for (const ParamTrack& p : node.animParams)
        if (!p.track.empty())
            if (float* v = animParamPtr(node, p.id)) *v = p.track.evaluate(frame).x;
    applyPla(node, frame);
    for (const auto& c : node.children()) evaluateAnimation(*c, frame);
}

void recordKey(SceneNode& node, int frame, bool position, bool rotation, bool scale) {
    if (position) node.animPos.set(frame, node.position);
    if (rotation) node.animRot.set(frame, node.rotationDeg);
    if (scale) node.animScale.set(frame, node.scale);
}

std::vector<int> keyFrames(const SceneNode& node) {
    std::vector<int> out;
    for (const AnimTrack* t : {&node.animPos, &node.animRot, &node.animScale})
        for (const AnimKey& k : t->keys) out.push_back(k.frame);
    for (const ParamTrack& p : node.animParams)
        for (const AnimKey& k : p.track.keys) out.push_back(k.frame);
    for (const PlaKey& k : node.animPla) out.push_back(k.frame);
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

namespace {
bool inFrames(const std::vector<int>& frames, int f) { return std::find(frames.begin(), frames.end(), f) != frames.end(); }

AnimTrack* tracksOf(SceneNode& n, int i) {
    return i == 0 ? &n.animPos : i == 1 ? &n.animRot : i == 2 ? &n.animScale : &n.animParams[(size_t)(i - 3)].track;
}
int trackCount(const SceneNode& n) { return 3 + (int)n.animParams.size(); }
} // namespace

int moveKeys(SceneNode& node, const std::vector<int>& frames, int delta) {
    if (delta == 0) return 0;
    int moved = 0;
    {
        std::vector<PlaKey> moving, staying;
        for (PlaKey& k : node.animPla) (inFrames(frames, k.frame) ? moving : staying).push_back(std::move(k));
        for (PlaKey& k : moving) k.frame += delta;
        staying.erase(std::remove_if(staying.begin(), staying.end(), [&](const PlaKey& s2) {
                          return std::any_of(moving.begin(), moving.end(), [&](const PlaKey& m) { return m.frame == s2.frame; });
                      }), staying.end());
        moved += (int)moving.size();
        for (PlaKey& k : moving) staying.push_back(std::move(k));
        std::sort(staying.begin(), staying.end(), [](const PlaKey& a, const PlaKey& b) { return a.frame < b.frame; });
        node.animPla = std::move(staying);
    }
    for (int ti = 0; ti < trackCount(node); ++ti) {
        AnimTrack& t = *tracksOf(node, ti);
        std::vector<AnimKey> moving, staying;
        for (const AnimKey& k : t.keys) (inFrames(frames, k.frame) ? moving : staying).push_back(k);
        if (moving.empty()) continue;
        for (AnimKey& k : moving) k.frame += delta;
        // Las que se mueven pisan a las que estaban en su sitio nuevo.
        staying.erase(std::remove_if(staying.begin(), staying.end(),
                                     [&](const AnimKey& s) {
                                         return std::any_of(moving.begin(), moving.end(),
                                                            [&](const AnimKey& m) { return m.frame == s.frame; });
                                     }),
                      staying.end());
        staying.insert(staying.end(), moving.begin(), moving.end());
        std::sort(staying.begin(), staying.end(), [](const AnimKey& a, const AnimKey& b) { return a.frame < b.frame; });
        t.keys = std::move(staying);
        moved += (int)moving.size();
    }
    return moved;
}

int deleteKeys(SceneNode& node, const std::vector<int>& frames) {
    int n = 0;
    {
        const size_t before = node.animPla.size();
        node.animPla.erase(std::remove_if(node.animPla.begin(), node.animPla.end(), [&](const PlaKey& k) { return inFrames(frames, k.frame); }),
                           node.animPla.end());
        n += (int)(before - node.animPla.size());
    }
    for (int ti = 0; ti < trackCount(node); ++ti) {
        auto& keys = tracksOf(node, ti)->keys;
        const size_t before = keys.size();
        keys.erase(std::remove_if(keys.begin(), keys.end(), [&](const AnimKey& k) { return inFrames(frames, k.frame); }),
                   keys.end());
        n += (int)(before - keys.size());
    }
    node.animParams.erase(std::remove_if(node.animParams.begin(), node.animParams.end(), [](const ParamTrack& p) { return p.track.empty(); }),
                          node.animParams.end());
    return n;
}

int setKeyInterp(SceneNode& node, const std::vector<int>& frames, KeyInterp interp) {
    int n = 0;
    for (int ti = 0; ti < trackCount(node); ++ti)
        for (AnimKey& k : tracksOf(node, ti)->keys)
            if (inFrames(frames, k.frame)) {
                k.interp = interp;
                ++n;
            }
    for (PlaKey& k : node.animPla)
        if (inFrames(frames, k.frame)) {
            k.interp = interp == KeyInterp::Step ? KeyInterp::Step : KeyInterp::Linear;
            ++n;
        }
    return n;
}

const char* lightKindName(LightKind kind) {
    switch (kind) {
        case LightKind::Omni: return "Omni";
        case LightKind::Spot: return "Foco";
        case LightKind::SpotSquare: return "Foco Cuadrado";
        case LightKind::Infinite: return "Infinita";
        case LightKind::Parallel: return "Paralela";
        case LightKind::ParallelSpot: return "Foco Paralelo";
        case LightKind::ParallelSpotSquare: return "Foco Paralelo Cuadrado";
        case LightKind::Tube: return "Tubo";
        case LightKind::Area: return "Área";
        case LightKind::Photometric: return "Fotométrica";
        default: return "Luz";
    }
}

float CameraData::fovDeg() const {
    if (focalMm <= 0.0f) return 0.0f;
    return 2.0f * std::atan(sensorMm * 0.5f / focalMm) * 57.29577951308232f;
}

SceneNode* duplicateSubtree(SceneNode& parent, const SceneNode& src) {
    return cloneInto(parent, src, src.name() + " copia");
}

bool pasteAttributes(SceneNode& dst, const SceneNode& src) {
    if (dst.primitive != src.primitive) return false;
    // Los atributos del objeto (no el nombre, la jerarquia ni las pistas de animacion).
    dst.position = src.position;
    dst.rotationDeg = src.rotationDeg;
    dst.scale = src.scale;
    dst.editorVis = src.editorVis;
    dst.renderVis = src.renderVis;
    dst.light = src.light;
    dst.camera = src.camera;
    dst.jointLimits = src.jointLimits;
    dst.protection = src.protection;
    dst.nullDisplay = src.nullDisplay;
    dst.useColor = src.useColor;
    dst.displayColor = src.displayColor;
    dst.xray = src.xray;
    dst.phongLimit = src.phongLimit;
    dst.phongAngle = src.phongAngle;
    dst.param = src.param;
    dst.spline = src.spline;
    dst.clone = src.clone;
    dst.deform.kind = src.deform.kind;
    dst.deform.axis = src.deform.axis;
    dst.deform.amount = src.deform.amount;
    dst.deform.param2 = src.deform.param2;
    dst.deform.formula = src.deform.formula;
    dst.env = src.env;
    return true;
}

std::string pathSegmentFor(const std::string& name, int occurrence) {
    return occurrence >= 2 ? name + "~#" + std::to_string(occurrence) : name;
}

std::string pathSegmentOf(const SceneNode& node) {
    int occurrence = 1;
    if (const SceneNode* p = node.parent())
        for (const auto& c : p->children()) {
            if (c.get() == &node) break;
            if (c->name() == node.name()) ++occurrence;
        }
    return pathSegmentFor(node.name(), occurrence);
}

SceneNode* childByPathSegment(const SceneNode& parent, const std::string& segment) {
    // Con sufijo "~#N": el N-esimo hijo con ese nombre.
    const size_t h = segment.rfind("~#");
    if (h != std::string::npos && h + 2 < segment.size() &&
        segment.find_first_not_of("0123456789", h + 2) == std::string::npos) {
        const std::string base = segment.substr(0, h);
        const int want = std::atoi(segment.c_str() + h + 2);
        int seen = 0;
        for (const auto& c : parent.children())
            if (c->name() == base && ++seen == want) return c.get();
    }
    // Sin sufijo (o un nombre que casualmente lo lleva): el primero con ese nombre exacto.
    for (const auto& c : parent.children())
        if (c->name() == segment) return c.get();
    return nullptr;
}

} // namespace iron3d
