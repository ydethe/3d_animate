#pragma once

#include <string>
#include <vector>

#include "mesh.hpp"
#include "types.hpp"

namespace animate {

// A decoded RGBA8 texture (empty when the submesh has none).
struct Texture {
    std::vector<unsigned char> rgba;
    int w = 0, h = 0;
    bool valid = false;
};

struct AssimpModel {
    std::vector<CpuMesh> meshes;      // solid submeshes (model space)
    std::vector<Texture> textures;    // parallel to meshes
    bool has_texture = false;
    std::vector<Triangle> triangles;  // zero-normal, for wireframe (spec §5.2)
};

// Load a non-STL model through Assimp (spec §3.2). Terminates with a user-facing
// error if the file cannot be loaded or contains no geometry.
AssimpModel load_assimp(const std::string& path);

}  // namespace animate
