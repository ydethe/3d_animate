#include "renderer.hpp"

#include <algorithm>
#include <string>
#include <vector>

#include "log.hpp"
#include "shaders.hpp"

namespace animate {
namespace {

// Vertical field of view. The spec fixes the camera position but not the lens;
// 40° comfortably frames the geometry (scaled to a bounding size of 3).
constexpr double kFovY = 40.0;

GLuint compile(GLenum type, const char* src) {
    GLuint sh = glCreateShader(type);
    glShaderSource(sh, 1, &src, nullptr);
    glCompileShader(sh);
    GLint ok = 0;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char buf[1024];
        glGetShaderInfoLog(sh, sizeof(buf), nullptr, buf);
        fatal(std::string("Shader compile failed: ") + buf);
    }
    return sh;
}

GLuint link_program(const char* vert, const char* frag) {
    GLuint v = compile(GL_VERTEX_SHADER, vert);
    GLuint f = compile(GL_FRAGMENT_SHADER, frag);
    GLuint prog = glCreateProgram();
    glAttachShader(prog, v);
    glAttachShader(prog, f);
    glLinkProgram(prog);
    GLint ok = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char buf[1024];
        glGetProgramInfoLog(prog, sizeof(buf), nullptr, buf);
        fatal(std::string("Program link failed: ") + buf);
    }
    glDeleteShader(v);
    glDeleteShader(f);
    return prog;
}

}  // namespace

Renderer::Renderer(const SceneConfig& cfg) : cfg_(cfg) {
    solid_prog_ = link_program(kSolidVert, kSolidFrag);
    line_prog_ = link_program(kLineVert, kLineFrag);

    view_ = Mat4::look_at({0, -8, 1.5}, {0, 0, 0}, {0, 0, 1});
    proj_ = Mat4::perspective(kFovY, (double)cfg.width / (double)cfg.height, 0.1, 100.0);

    // Light directions point toward the light (negated shining direction).
    key_dir_ = (hpr_to_forward(-30, -60) * -1.0).normalized();
    fill_dir_ = (hpr_to_forward(150, -20) * -1.0).normalized();
}

Renderer::~Renderer() {
    if (solid_prog_) glDeleteProgram(solid_prog_);
    if (line_prog_) glDeleteProgram(line_prog_);
}

void Renderer::set_solid(std::vector<GlMesh> meshes) { meshes_ = std::move(meshes); }

void Renderer::set_lines(GlLines lines) {
    lines_ = std::move(lines);
    has_lines_ = true;
}

void Renderer::set_bounds(const Vec3& min_b, const Vec3& max_b) {
    Vec3 center = (min_b + max_b) * 0.5;
    Vec3 size = max_b - min_b;
    double largest = std::max(std::max(size.x, size.y), std::max(size.z, 1e-6));
    double s = 3.0 / largest;
    framing_ = Mat4::translate(center * (-s)) * Mat4::scale(s);
}

void Renderer::render_frame(double heading_deg) {
    glViewport(0, 0, cfg_.width, cfg_.height);
    glClearColor(cfg_.background.r, cfg_.background.g, cfg_.background.b,
                 cfg_.background.a);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);

    Mat4 model = Mat4::rotate_z(heading_deg) * framing_;
    Mat4 mvp = proj_ * view_ * model;

    if (cfg_.wireframe) {
        glUseProgram(line_prog_);
        glUniformMatrix4fv(glGetUniformLocation(line_prog_, "uMVP"), 1, GL_FALSE, mvp.m.data());
        glUniform4f(glGetUniformLocation(line_prog_, "uColor"), cfg_.edge_color.r,
                    cfg_.edge_color.g, cfg_.edge_color.b, 1.0f);
        glLineWidth(2.0f);
        if (has_lines_) lines_.draw();
        return;
    }

    glUseProgram(solid_prog_);
    glUniformMatrix4fv(glGetUniformLocation(solid_prog_, "uMVP"), 1, GL_FALSE, mvp.m.data());
    glUniformMatrix4fv(glGetUniformLocation(solid_prog_, "uModel"), 1, GL_FALSE, model.m.data());
    glUniform3f(glGetUniformLocation(solid_prog_, "uKeyDir"), (float)key_dir_.x,
                (float)key_dir_.y, (float)key_dir_.z);
    glUniform3f(glGetUniformLocation(solid_prog_, "uKeyColor"), 1.0f, 0.98f, 0.9f);
    glUniform3f(glGetUniformLocation(solid_prog_, "uFillDir"), (float)fill_dir_.x,
                (float)fill_dir_.y, (float)fill_dir_.z);
    glUniform3f(glGetUniformLocation(solid_prog_, "uFillColor"), 0.35f, 0.4f, 0.5f);
    glUniform3f(glGetUniformLocation(solid_prog_, "uAmbient"), 0.25f, 0.25f, 0.3f);
    glUniform1i(glGetUniformLocation(solid_prog_, "uTex"), 0);

    GLint loc_use = glGetUniformLocation(solid_prog_, "uUseTexture");
    GLint loc_base = glGetUniformLocation(solid_prog_, "uBaseColor");
    for (const GlMesh& mesh : meshes_) {
        if (mesh.textured()) {
            glUniform1i(loc_use, 1);
            mesh.bind_texture();
        } else {
            glUniform1i(loc_use, 0);
            glUniform3f(loc_base, cfg_.base_color.r, cfg_.base_color.g, cfg_.base_color.b);
        }
        mesh.draw();
    }
}

}  // namespace animate
