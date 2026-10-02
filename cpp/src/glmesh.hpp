#pragma once

#include <string>

#include "gl.hpp"
#include "mesh.hpp"

namespace animate {

// GPU triangle mesh: interleaved position(3)+normal(3)+uv(2), indexed, with an
// optional diffuse texture.
class GlMesh {
public:
    GlMesh() = default;
    ~GlMesh();
    GlMesh(const GlMesh&) = delete;
    GlMesh& operator=(const GlMesh&) = delete;
    GlMesh(GlMesh&& o) noexcept { move_from(o); }
    GlMesh& operator=(GlMesh&& o) noexcept {
        if (this != &o) { destroy(); move_from(o); }
        return *this;
    }

    void upload(const CpuMesh& mesh);
    // Upload a decoded RGBA8 diffuse texture (tightly packed, `w`*`h`*4 bytes).
    void set_texture_rgba(const unsigned char* rgba, int w, int h);

    bool textured() const { return tex_ != 0; }
    void bind_texture() const;  // binds to texture unit 0
    void draw() const;

private:
    void destroy();
    void move_from(GlMesh& o);

    GLuint vao_ = 0, vbo_ = 0, ebo_ = 0, tex_ = 0;
    GLsizei index_count_ = 0;
};

// GPU line mesh: position-only, indexed line pairs.
class GlLines {
public:
    GlLines() = default;
    ~GlLines();
    GlLines(const GlLines&) = delete;
    GlLines& operator=(const GlLines&) = delete;
    GlLines(GlLines&& o) noexcept { move_from(o); }
    GlLines& operator=(GlLines&& o) noexcept {
        if (this != &o) { destroy(); move_from(o); }
        return *this;
    }

    void upload(const LineMesh& lines);
    void draw() const;

private:
    void destroy();
    void move_from(GlLines& o);

    GLuint vao_ = 0, vbo_ = 0, ebo_ = 0;
    GLsizei index_count_ = 0;
};

}  // namespace animate
