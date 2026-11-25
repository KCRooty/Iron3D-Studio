#include "importers/abc/AbcIO.h"

#include "core/MeshData.h"

#include <Alembic/AbcCoreOgawa/All.h>
#include <Alembic/AbcGeom/All.h>

#include <algorithm>
#include <cctype>
#include <set>
#include <type_traits>

namespace fs = std::filesystem;
namespace Abc = Alembic::Abc;
namespace Geom = Alembic::AbcGeom;

namespace iron3d {

bool isAbcPath(const fs::path& path) {
    std::string e = path.extension().string();
    std::transform(e.begin(), e.end(), e.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    return e == ".abc";
}

namespace {

glm::mat4 toGlm(const Abc::M44d& m) {
    glm::mat4 r(1.0f);
    for (int c = 0; c < 4; ++c)
        for (int rr = 0; rr < 4; ++rr) r[c][rr] = (float)m[c][rr]; // Imath: fila-mayor con vectores fila = glm columna
    return r;
}

Abc::M44d fromGlm(const glm::mat4& m) {
    Abc::M44d r;
    for (int c = 0; c < 4; ++c)
        for (int rr = 0; rr < 4; ++rr) r[c][rr] = (double)m[c][rr];
    return r;
}

struct MeshSamples {
    Abc::P3fArraySamplePtr P;
    Abc::Int32ArraySamplePtr idx, cnt;
    Abc::N3fArraySamplePtr N;
    Abc::V2fArraySamplePtr UV;
    Abc::UInt32ArraySamplePtr UVidx;
    bool normalsPerCorner = false, uvsPerCorner = false;
};

void buildMesh(const MeshSamples& ms, SceneNode& node, AbcReport& rep) {
    if (!ms.P || !ms.idx || !ms.cnt) return;
    auto mesh = std::make_shared<MeshData>();
    const size_t nCorners = ms.idx->size();
    size_t cornerBase = 0;
    for (size_t f = 0; f < ms.cnt->size(); ++f) {
        const int n = (*ms.cnt)[f];
        if (n >= 3 && cornerBase + (size_t)n <= nCorners) {
            // Abanico de triangulos con el sentido invertido (Alembic es horario).
            for (int k = 1; k + 1 < n; ++k) {
                const size_t c3[3] = {cornerBase, cornerBase + (size_t)k + 1, cornerBase + (size_t)k};
                for (size_t c : c3) {
                    Vertex v{};
                    const int32_t vi = (*ms.idx)[c];
                    if (vi < 0 || (size_t)vi >= ms.P->size()) continue;
                    const Abc::V3f p = (*ms.P)[(size_t)vi];
                    v.position = glm::vec3(p.x, p.y, p.z);
                    if (ms.N) {
                        const size_t ni = ms.normalsPerCorner ? c : (size_t)vi;
                        if (ni < ms.N->size()) v.normal = glm::vec3((*ms.N)[ni].x, (*ms.N)[ni].y, (*ms.N)[ni].z);
                    }
                    if (ms.UV) {
                        size_t ui = ms.uvsPerCorner ? c : (size_t)vi;
                        if (ms.UVidx && ui < ms.UVidx->size()) ui = (*ms.UVidx)[ui];
                        if (ui < ms.UV->size()) v.uv = glm::vec2((*ms.UV)[ui].x, 1.0f - (*ms.UV)[ui].y);
                    }
                    mesh->indices.push_back((uint32_t)mesh->vertices.size());
                    mesh->vertices.push_back(v);
                }
                mesh->faceTris.push_back(1);
            }
        }
        cornerBase += (size_t)std::max(n, 0);
    }
    if (mesh->indices.size() < 3) return;
    // Normales planas donde el archivo no las trae.
    if (!ms.N) {
        for (size_t i = 0; i + 2 < mesh->indices.size(); i += 3) {
            const glm::vec3 a = mesh->vertices[mesh->indices[i]].position, b = mesh->vertices[mesh->indices[i + 1]].position,
                            c = mesh->vertices[mesh->indices[i + 2]].position;
            glm::vec3 n = glm::cross(b - a, c - a);
            n = glm::dot(n, n) > 1e-20f ? glm::normalize(n) : glm::vec3(0, 1, 0);
            for (int k = 0; k < 3; ++k) mesh->vertices[mesh->indices[i + (size_t)k]].normal = n;
        }
    }
    updateBounds(*mesh);
    rep.points += (long long)mesh->vertices.size();
    rep.polygons += (long long)(mesh->indices.size() / 3);
    ++rep.meshes;
    node.primitive = PrimitiveType::Mesh;
    node.materialIndex = -1;
    node.mesh = std::move(mesh);
}

template <class Schema>
void readNormalsUvs(Schema& schema, MeshSamples& ms) {
    if constexpr (std::is_same_v<Schema, Geom::IPolyMeshSchema>) try {
        if (schema.getNormalsParam().valid()) {
            Geom::IN3fGeomParam::Sample ns = schema.getNormalsParam().getExpandedValue();
            ms.N = ns.getVals();
            ms.normalsPerCorner = ns.getScope() == Geom::kFacevaryingScope || (ms.N && ms.idx && ms.N->size() == ms.idx->size());
        }
    } catch (...) {
        ms.N.reset();
    }
    try {
        if (schema.getUVsParam().valid()) {
            Geom::IV2fGeomParam::Sample us = schema.getUVsParam().getIndexedValue();
            ms.UV = us.getVals();
            ms.UVidx = us.getIndices();
            const size_t n = ms.UVidx ? ms.UVidx->size() : (ms.UV ? ms.UV->size() : 0);
            ms.uvsPerCorner = us.getScope() == Geom::kFacevaryingScope || (ms.idx && n == ms.idx->size());
        }
    } catch (...) {
        ms.UV.reset();
        ms.UVidx.reset();
    }
}

void walk(const Abc::IObject& obj, SceneNode& parent, AbcReport& rep, int depth) {
    if (depth > 200) return;
    for (size_t i = 0; i < obj.getNumChildren(); ++i) {
        Abc::IObject ch = obj.getChild(i);
        const Abc::ObjectHeader& h = ch.getHeader();
        if (Geom::IXform::matches(h)) {
            Geom::IXform x(ch, Abc::kWrapExisting);
            Geom::XformSample xs;
            x.getSchema().get(xs, Abc::ISampleSelector((Abc::index_t)0));
            SceneNode* n = parent.addChild(h.getName());
            n->setLocalMatrix(toGlm(xs.getMatrix()));
            ++rep.nodes;
            walk(ch, *n, rep, depth + 1);
        } else if (Geom::IPolyMesh::matches(h)) {
            Geom::IPolyMesh pm(ch, Abc::kWrapExisting);
            Geom::IPolyMeshSchema::Sample smp;
            pm.getSchema().get(smp, Abc::ISampleSelector((Abc::index_t)0));
            MeshSamples ms;
            ms.P = smp.getPositions();
            ms.idx = smp.getFaceIndices();
            ms.cnt = smp.getFaceCounts();
            readNormalsUvs(pm.getSchema(), ms);
            SceneNode* n = parent.addChild(h.getName());
            ++rep.nodes;
            buildMesh(ms, *n, rep);
        } else if (Geom::ISubD::matches(h)) {
            Geom::ISubD sd(ch, Abc::kWrapExisting);
            Geom::ISubDSchema::Sample smp;
            sd.getSchema().get(smp, Abc::ISampleSelector((Abc::index_t)0));
            MeshSamples ms;
            ms.P = smp.getPositions();
            ms.idx = smp.getFaceIndices();
            ms.cnt = smp.getFaceCounts();
            readNormalsUvs(sd.getSchema(), ms);
            SceneNode* n = parent.addChild(h.getName());
            ++rep.nodes;
            buildMesh(ms, *n, rep);
        } else if (Geom::ICamera::matches(h) || Geom::ICurves::matches(h) || Geom::IPoints::matches(h)) {
            rep.warnings.push_back("Objeto '" + h.getName() + "' (cámara, curvas o puntos): todavía no se importa.");
        } else {
            walk(ch, parent, rep, depth + 1);
        }
    }
}

} // namespace

bool importAbc(const fs::path& path, SceneNode& root, std::vector<Material>& materials, AbcReport& report,
               std::string* error) {
    (void)materials;
    try {
        Abc::IArchive ar(Alembic::AbcCoreOgawa::ReadArchive(), path.string());
        if (!ar.valid()) {
            if (error) *error = "El archivo no es un Alembic Ogawa válido (los .abc antiguos de HDF5 no se abren).";
            return false;
        }
        walk(ar.getTop(), root, report, 0);
        report.warnings.push_back("Alembic: se importa el primer fotograma; la animación de vértices todavía no.");
        return true;
    } catch (const std::exception& e) {
        if (error) *error = e.what();
        return false;
    } catch (...) {
        if (error) *error = "Error desconocido leyendo el .abc.";
        return false;
    }
}

// ---------------------------------------------------------------------------------------------
namespace {

MeshData meshOfNode(const SceneNode& n) {
    if (n.mesh) return *n.mesh;
    switch (n.primitive) {
        case PrimitiveType::Cube: return makeCubeMesh();
        case PrimitiveType::Sphere: return makeSphereMesh();
        case PrimitiveType::Plane: return makePlaneMesh();
        default: return {};
    }
}

std::string safeName(const std::string& s, std::set<std::string>& used) {
    std::string o;
    for (char c : s) o += (std::isalnum((unsigned char)c) || c == '_' || c == '-' || c == '.') ? c : '_';
    if (o.empty()) o = "nodo";
    std::string out = o;
    for (int k = 1; used.count(out); ++k) out = o + "_" + std::to_string(k);
    used.insert(out);
    return out;
}

struct SibNames {
    std::set<std::string> used;
};

void exportRec(const SceneNode& n, Abc::OObject parent, SibNames& sib, AbcReport& rep) {
    const std::string name = safeName(n.name(), sib.used);
    Geom::OXform xf(parent, name);
    Geom::XformSample xs;
    xs.setMatrix(fromGlm(n.localMatrix()));
    xf.getSchema().set(xs);
    ++rep.nodes;
    const MeshData mesh = meshOfNode(n);
    if (!mesh.vertices.empty() && mesh.indices.size() >= 3) {
        const size_t ntri = mesh.indices.size() / 3;
        std::vector<Abc::V3f> P;
        P.reserve(mesh.vertices.size());
        for (const Vertex& v : mesh.vertices) P.emplace_back(v.position.x, v.position.y, v.position.z);
        std::vector<int32_t> idx(ntri * 3), cnt(ntri, 3);
        std::vector<Abc::N3f> N(ntri * 3);
        std::vector<Abc::V2f> UV(ntri * 3);
        for (size_t t = 0; t < ntri; ++t) {
            // Sentido horario: se invierte el orden de los triangulos de Iron3D.
            const size_t order[3] = {0, 2, 1};
            for (int k = 0; k < 3; ++k) {
                const uint32_t vi = mesh.indices[t * 3 + order[k]];
                idx[t * 3 + (size_t)k] = (int32_t)vi;
                const Vertex& v = mesh.vertices[vi];
                N[t * 3 + (size_t)k] = Abc::N3f(v.normal.x, v.normal.y, v.normal.z);
                UV[t * 3 + (size_t)k] = Abc::V2f(v.uv.x, 1.0f - v.uv.y);
            }
        }
        std::set<std::string> none;
        Geom::OPolyMesh pm(xf, safeName(name + "Shape", none));
        Geom::OPolyMeshSchema::Sample smp(Geom::V3fArraySample(P.data(), P.size()), Geom::Int32ArraySample(idx.data(), idx.size()),
                                         Geom::Int32ArraySample(cnt.data(), cnt.size()));
        smp.setNormals(Geom::ON3fGeomParam::Sample(Geom::N3fArraySample(N.data(), N.size()), Geom::kFacevaryingScope));
        smp.setUVs(Geom::OV2fGeomParam::Sample(Geom::V2fArraySample(UV.data(), UV.size()), Geom::kFacevaryingScope));
        pm.getSchema().set(smp);
        rep.points += (long long)P.size();
        rep.polygons += (long long)ntri;
        ++rep.meshes;
    }
    SibNames childNames;
    for (const auto& c : n.children()) exportRec(*c, Abc::OObject(xf), childNames, rep);
}

} // namespace

bool exportAbc(const fs::path& path, const std::vector<const SceneNode*>& roots, const std::vector<Material>& materials,
               AbcReport& report, std::string* error) {
    (void)materials;
    try {
        Abc::OArchive ar(Alembic::AbcCoreOgawa::WriteArchive(), path.string());
        SibNames top;
        for (const SceneNode* r : roots) exportRec(*r, ar.getTop(), top, report);
        return true;
    } catch (const std::exception& e) {
        if (error) *error = e.what();
        return false;
    } catch (...) {
        if (error) *error = "Error desconocido escribiendo el .abc.";
        return false;
    }
}

} // namespace iron3d
