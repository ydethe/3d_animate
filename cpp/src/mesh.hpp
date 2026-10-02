#pragma once

#include <vector>

#include "types.hpp"

namespace animate {

// CPU-side renderable triangle mesh. `uvs` holds 2 floats per vertex and may be
// empty; `normals` may be empty for untextured line-only use.
struct CpuMesh {
    std::vector<Vec3> positions;
    std::vector<Vec3> normals;
    std::vector<float> uvs;
    std::vector<unsigned int> indices;
};

// CPU-side line mesh (index pairs into `positions`).
struct LineMesh {
    std::vector<Vec3> positions;
    std::vector<unsigned int> indices;
};

// Weld STL triangles by exact coordinate equality and accumulate smooth
// per-vertex normals (spec §5.1).
CpuMesh stl_to_solid(const std::vector<Triangle>& triangles);

// Build the facet-aware outline: only perimeter edges of each flat facet
// (spec §5.3).
LineMesh build_facet_outline(const std::vector<Triangle>& triangles);

// Decimate an STL triangle list to ~`ratio` of its face count via quadric
// error metric edge collapse (spec §4.1). `ratio` must be in (0, 1].
std::vector<Triangle> simplify_stl(const std::vector<Triangle>& triangles,
                                   double ratio);

}  // namespace animate
