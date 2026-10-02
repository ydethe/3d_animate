#pragma once

#include <vector>

#include "gl.hpp"
#include "glmesh.hpp"
#include "types.hpp"

namespace animate {

struct SceneConfig {
    bool wireframe = false;
    Color base_color;   // flat tint for untextured solid models
    Color edge_color;   // wireframe line color
    Color background;   // clear color (rgba; alpha 0 => transparent)
    int width = 1920;
    int height = 1080;
};

// Owns the GL programs and scene state; renders one framed, lit, rotated frame.
class Renderer {
public:
    explicit Renderer(const SceneConfig& cfg);
    ~Renderer();

    void set_solid(std::vector<GlMesh> meshes);
    void set_lines(GlLines lines);
    // Axis-aligned bounds of the source geometry, used for centering/scaling.
    void set_bounds(const Vec3& min_b, const Vec3& max_b);

    void render_frame(double heading_deg);

private:
    SceneConfig cfg_;
    std::vector<GlMesh> meshes_;
    GlLines lines_;
    bool has_lines_ = false;

    Mat4 framing_ = Mat4::identity();
    Mat4 view_ = Mat4::identity();
    Mat4 proj_ = Mat4::identity();

    // Light directions (toward the light) and colors, precomputed.
    Vec3 key_dir_, fill_dir_;

    GLuint solid_prog_ = 0, line_prog_ = 0;
};

}  // namespace animate
