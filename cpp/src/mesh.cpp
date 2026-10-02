#include "mesh.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <map>
#include <set>
#include <unordered_map>
#include <utility>

#include "log.hpp"

// Simplify.h defines global variables inside namespace Simplify, so it must be
// included in exactly one translation unit. This is that unit.
#include "Simplify.h"

namespace animate {
namespace {

// Hashable key for welding vertices by exact coordinate equality. Uses the raw
// double bit patterns so equality is exact (matching the Python dict behaviour).
struct VertKey {
    uint64_t x, y, z;
    bool operator==(const VertKey& o) const {
        return x == o.x && y == o.y && z == o.z;
    }
};

VertKey make_key(const Vec3& v) {
    VertKey k;
    std::memcpy(&k.x, &v.x, 8);
    std::memcpy(&k.y, &v.y, 8);
    std::memcpy(&k.z, &v.z, 8);
    return k;
}

struct VertKeyHash {
    size_t operator()(const VertKey& k) const {
        size_t h = std::hash<uint64_t>{}(k.x);
        h ^= std::hash<uint64_t>{}(k.y) + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
        h ^= std::hash<uint64_t>{}(k.z) + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
        return h;
    }
};

// Weld triangle vertices into a unique position list + integer face list.
struct Indexed {
    std::vector<Vec3> positions;
    std::vector<std::array<int, 3>> faces;
};

Indexed weld(const std::vector<Triangle>& tris) {
    Indexed out;
    std::unordered_map<VertKey, int, VertKeyHash> index_of;
    index_of.reserve(tris.size() * 3);
    for (const Triangle& t : tris) {
        std::array<int, 3> face{};
        for (int i = 0; i < 3; ++i) {
            VertKey k = make_key(t.v[i]);
            auto it = index_of.find(k);
            int idx;
            if (it == index_of.end()) {
                idx = static_cast<int>(out.positions.size());
                index_of.emplace(k, idx);
                out.positions.push_back(t.v[i]);
            } else {
                idx = it->second;
            }
            face[i] = idx;
        }
        out.faces.push_back(face);
    }
    return out;
}

Vec3 face_normal(const Vec3& v0, const Vec3& v1, const Vec3& v2) {
    Vec3 n = (v1 - v0).cross(v2 - v0);
    return n.length_squared() > 0 ? n.normalized() : Vec3{0, 0, 0};
}

// Disjoint-set union for facet grouping.
struct DSU {
    std::vector<int> parent;
    explicit DSU(int n) : parent(n) {
        for (int i = 0; i < n; ++i) parent[i] = i;
    }
    int find(int a) {
        while (parent[a] != a) {
            parent[a] = parent[parent[a]];
            a = parent[a];
        }
        return a;
    }
    void unite(int a, int b) { parent[find(a)] = find(b); }
};

constexpr double kCoplanarCosTol = 1.0 - 1e-4;  // ~0.8 degrees; see spec §5.3

}  // namespace

CpuMesh stl_to_solid(const std::vector<Triangle>& triangles) {
    CpuMesh mesh;
    std::unordered_map<VertKey, int, VertKeyHash> index_of;
    index_of.reserve(triangles.size() * 3);

    for (const Triangle& t : triangles) {
        Vec3 n = t.normal;
        if (n.length_squared() == 0) n = face_normal(t.v[0], t.v[1], t.v[2]);
        for (int i = 0; i < 3; ++i) {
            VertKey k = make_key(t.v[i]);
            auto it = index_of.find(k);
            int idx;
            if (it == index_of.end()) {
                idx = static_cast<int>(mesh.positions.size());
                index_of.emplace(k, idx);
                mesh.positions.push_back(t.v[i]);
                mesh.normals.push_back({0, 0, 0});
            } else {
                idx = it->second;
            }
            mesh.normals[idx] = mesh.normals[idx] + n;  // accumulate
            mesh.indices.push_back(static_cast<unsigned int>(idx));
        }
    }
    for (Vec3& n : mesh.normals) {
        if (n.length_squared() > 0) n = n.normalized();
    }
    return mesh;
}

LineMesh build_facet_outline(const std::vector<Triangle>& triangles) {
    Indexed idx = weld(triangles);
    const int nf = static_cast<int>(idx.faces.size());

    std::vector<Vec3> fn(nf);
    for (int i = 0; i < nf; ++i) {
        const auto& f = idx.faces[i];
        fn[i] = face_normal(idx.positions[f[0]], idx.positions[f[1]], idx.positions[f[2]]);
    }

    // Map each undirected edge to the faces that use it.
    auto edge_key = [](int a, int b) {
        if (a > b) std::swap(a, b);
        return std::pair<int, int>(a, b);
    };
    std::map<std::pair<int, int>, std::vector<int>> edge_faces;
    for (int i = 0; i < nf; ++i) {
        const auto& f = idx.faces[i];
        edge_faces[edge_key(f[0], f[1])].push_back(i);
        edge_faces[edge_key(f[1], f[2])].push_back(i);
        edge_faces[edge_key(f[2], f[0])].push_back(i);
    }

    // Group adjacent coplanar faces into facets.
    DSU dsu(std::max(nf, 1));
    for (const auto& [edge, faces] : edge_faces) {
        for (size_t a = 0; a + 1 < faces.size(); ++a) {
            for (size_t b = a + 1; b < faces.size(); ++b) {
                if (fn[faces[a]].dot(fn[faces[b]]) >= kCoplanarCosTol) {
                    dsu.unite(faces[a], faces[b]);
                }
            }
        }
    }

    // Collect components; a facet is a component with >= 2 faces.
    std::unordered_map<int, std::vector<int>> groups;
    for (int i = 0; i < nf; ++i) groups[dsu.find(i)].push_back(i);

    std::set<std::pair<int, int>> outline;
    std::vector<char> in_facet(nf, 0);

    for (const auto& [root, members] : groups) {
        if (members.size() < 2) continue;  // not a facet
        std::map<std::pair<int, int>, int> count;
        for (int fi : members) {
            in_facet[fi] = 1;
            const auto& f = idx.faces[fi];
            count[edge_key(f[0], f[1])]++;
            count[edge_key(f[1], f[2])]++;
            count[edge_key(f[2], f[0])]++;
        }
        for (const auto& [edge, c] : count) {
            if (c == 1) outline.insert(edge);  // facet-boundary edge -> drawn
        }
    }

    // Faces in no facet: all three edges drawn.
    for (int i = 0; i < nf; ++i) {
        if (in_facet[i]) continue;
        const auto& f = idx.faces[i];
        outline.insert(edge_key(f[0], f[1]));
        outline.insert(edge_key(f[1], f[2]));
        outline.insert(edge_key(f[2], f[0]));
    }

    LineMesh lm;
    lm.positions = std::move(idx.positions);
    for (const auto& [a, b] : outline) {
        lm.indices.push_back(static_cast<unsigned int>(a));
        lm.indices.push_back(static_cast<unsigned int>(b));
    }
    return lm;
}

std::vector<Triangle> simplify_stl(const std::vector<Triangle>& triangles,
                                   double ratio) {
    Indexed idx = weld(triangles);
    const int n_faces = static_cast<int>(idx.faces.size());
    int target = std::max(1, static_cast<int>(std::lround(n_faces * ratio)));
    log_info("Simplifying mesh: %d -> ~%d faces (ratio %.2f)", n_faces, target, ratio);

    Simplify::vertices.clear();
    Simplify::triangles.clear();
    Simplify::refs.clear();

    Simplify::vertices.reserve(idx.positions.size());
    for (const Vec3& p : idx.positions) {
        Simplify::Vertex v;
        v.p = vec3f(p.x, p.y, p.z);
        Simplify::vertices.push_back(v);
    }
    Simplify::triangles.reserve(idx.faces.size());
    for (const auto& f : idx.faces) {
        Simplify::Triangle t;
        t.v[0] = f[0];
        t.v[1] = f[1];
        t.v[2] = f[2];
        t.deleted = 0;
        t.attr = 0;  // no UV/normal/color attributes to carry through
        t.material = -1;
        Simplify::triangles.push_back(t);
    }

    Simplify::simplify_mesh(target, 7.0, /*verbose=*/false);

    std::vector<Triangle> result;
    result.reserve(Simplify::triangles.size());
    for (const auto& t : Simplify::triangles) {
        if (t.deleted) continue;
        Vec3 a{Simplify::vertices[t.v[0]].p.x, Simplify::vertices[t.v[0]].p.y,
               Simplify::vertices[t.v[0]].p.z};
        Vec3 b{Simplify::vertices[t.v[1]].p.x, Simplify::vertices[t.v[1]].p.y,
               Simplify::vertices[t.v[1]].p.z};
        Vec3 c{Simplify::vertices[t.v[2]].p.x, Simplify::vertices[t.v[2]].p.y,
               Simplify::vertices[t.v[2]].p.z};
        Triangle tri;
        tri.normal = face_normal(a, b, c);
        tri.v[0] = a;
        tri.v[1] = b;
        tri.v[2] = c;
        result.push_back(tri);
    }

    log_info("Simplified to %zu faces", result.size());
    return result;
}

}  // namespace animate
