#include "assimp_load.hpp"

#include <assimp/material.h>
#include <assimp/mesh.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <assimp/texture.h>

#include <assimp/Importer.hpp>
#include <cstdlib>
#include <filesystem>

#include "log.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

namespace animate {
namespace {

namespace fs = std::filesystem;

// Panda3D's loaders present models in a Z-up frame; Assimp keeps most formats
// (glTF, FBX, …) Y-up. Convert Y-up right-handed -> Z-up right-handed so models
// stand upright and spin about the vertical axis as in the reference.
inline Vec3 y_up_to_z_up(float x, float y, float z) {
    return Vec3{x, -z, y};
}

// Decode raw image bytes (file contents or embedded blob) to flipped RGBA8.
Texture decode_memory(const unsigned char* bytes, int len) {
    Texture tex;
    int w, h, n;
    stbi_set_flip_vertically_on_load(1);
    unsigned char* px = stbi_load_from_memory(bytes, len, &w, &h, &n, 4);
    if (!px) return tex;
    tex.rgba.assign(px, px + static_cast<size_t>(w) * h * 4);
    tex.w = w;
    tex.h = h;
    tex.valid = true;
    stbi_image_free(px);
    return tex;
}

Texture decode_file(const std::string& path) {
    Texture tex;
    int w, h, n;
    stbi_set_flip_vertically_on_load(1);
    unsigned char* px = stbi_load(path.c_str(), &w, &h, &n, 4);
    if (!px) {
        log_warn("Could not load texture '%s': %s", path.c_str(), stbi_failure_reason());
        return tex;
    }
    tex.rgba.assign(px, px + static_cast<size_t>(w) * h * 4);
    tex.w = w;
    tex.h = h;
    tex.valid = true;
    stbi_image_free(px);
    return tex;
}

// Resolve the diffuse texture for a material, handling embedded and external
// references. Textures are resolved relative to the model's own directory.
Texture resolve_texture(const aiScene* scene, const aiMaterial* mat,
                        const fs::path& model_dir) {
    if (mat->GetTextureCount(aiTextureType_DIFFUSE) == 0) return {};
    aiString ai_path;
    if (mat->GetTexture(aiTextureType_DIFFUSE, 0, &ai_path) != AI_SUCCESS) return {};
    std::string p = ai_path.C_Str();
    if (p.empty()) return {};

    // Embedded texture reference: "*<index>".
    if (p[0] == '*') {
        int idx = std::atoi(p.c_str() + 1);
        if (idx < 0 || idx >= static_cast<int>(scene->mNumTextures)) return {};
        const aiTexture* t = scene->mTextures[idx];
        if (t->mHeight == 0) {
            // Compressed blob (png/jpg/…) of mWidth bytes.
            return decode_memory(reinterpret_cast<const unsigned char*>(t->pcData),
                                 static_cast<int>(t->mWidth));
        }
        // Uncompressed aiTexel array (BGRA).
        Texture tex;
        tex.w = static_cast<int>(t->mWidth);
        tex.h = static_cast<int>(t->mHeight);
        tex.rgba.resize(static_cast<size_t>(tex.w) * tex.h * 4);
        for (size_t i = 0; i < static_cast<size_t>(tex.w) * tex.h; ++i) {
            const aiTexel& tx = t->pcData[i];
            tex.rgba[i * 4 + 0] = tx.r;
            tex.rgba[i * 4 + 1] = tx.g;
            tex.rgba[i * 4 + 2] = tx.b;
            tex.rgba[i * 4 + 3] = tx.a;
        }
        tex.valid = true;
        return tex;
    }

    // External file, resolved relative to the model directory (then as-is).
    fs::path rel = model_dir / p;
    if (fs::exists(rel)) return decode_file(rel.string());
    if (fs::exists(p)) return decode_file(p);
    // Some exporters store a full path; try just the filename next to the model.
    fs::path base = model_dir / fs::path(p).filename();
    if (fs::exists(base)) return decode_file(base.string());
    log_warn("Texture '%s' referenced by model not found near %s", p.c_str(),
             model_dir.string().c_str());
    return {};
}

}  // namespace

AssimpModel load_assimp(const std::string& path) {
    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(
        path, aiProcess_Triangulate | aiProcess_GenSmoothNormals |
                  aiProcess_PreTransformVertices | aiProcess_JoinIdenticalVertices);

    if (!scene || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) || !scene->mRootNode ||
        scene->mNumMeshes == 0) {
        fatal("Could not load '" + path + "' — unsupported or corrupt file? (" +
              importer.GetErrorString() + ")");
    }

    const fs::path model_dir = fs::path(path).parent_path();
    AssimpModel out;

    for (unsigned mi = 0; mi < scene->mNumMeshes; ++mi) {
        const aiMesh* m = scene->mMeshes[mi];
        if (m->mNumVertices == 0 || m->mNumFaces == 0) continue;

        CpuMesh cm;
        cm.positions.reserve(m->mNumVertices);
        cm.normals.reserve(m->mNumVertices);
        cm.uvs.reserve(m->mNumVertices * 2);
        const bool has_uv = m->HasTextureCoords(0);

        for (unsigned vi = 0; vi < m->mNumVertices; ++vi) {
            const aiVector3D& v = m->mVertices[vi];
            cm.positions.push_back(y_up_to_z_up(v.x, v.y, v.z));
            if (m->HasNormals()) {
                const aiVector3D& n = m->mNormals[vi];
                cm.normals.push_back(y_up_to_z_up(n.x, n.y, n.z).normalized());
            } else {
                cm.normals.push_back({0, 0, 1});
            }
            if (has_uv) {
                cm.uvs.push_back(m->mTextureCoords[0][vi].x);
                cm.uvs.push_back(m->mTextureCoords[0][vi].y);
            } else {
                cm.uvs.push_back(0.f);
                cm.uvs.push_back(0.f);
            }
        }

        for (unsigned fi = 0; fi < m->mNumFaces; ++fi) {
            const aiFace& f = m->mFaces[fi];
            if (f.mNumIndices != 3) continue;  // triangulated already
            cm.indices.push_back(f.mIndices[0]);
            cm.indices.push_back(f.mIndices[1]);
            cm.indices.push_back(f.mIndices[2]);

            // Accumulate raw triangles for wireframe extraction (spec §5.2).
            Triangle tri;
            tri.normal = {0, 0, 0};
            for (int k = 0; k < 3; ++k) tri.v[k] = cm.positions[f.mIndices[k]];
            out.triangles.push_back(tri);
        }

        Texture tex = resolve_texture(scene, scene->mMaterials[m->mMaterialIndex],
                                      model_dir);
        if (tex.valid) out.has_texture = true;

        out.meshes.push_back(std::move(cm));
        out.textures.push_back(std::move(tex));
    }

    if (out.meshes.empty()) {
        fatal("Model '" + path + "' loaded but contained no triangle geometry.");
    }
    return out;
}

}  // namespace animate
