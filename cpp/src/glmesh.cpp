#include "glmesh.hpp"

#include "log.hpp"

namespace animate {

namespace {
// Interleave a CpuMesh into position(3)+normal(3)+uv(2) float records.
std::vector<float> interleave(const CpuMesh& mesh) {
    std::vector<float> data;
    data.reserve(mesh.positions.size() * 8);
    for (size_t i = 0; i < mesh.positions.size(); ++i) {
        const Vec3& p = mesh.positions[i];
        data.push_back((float)p.x);
        data.push_back((float)p.y);
        data.push_back((float)p.z);
        if (i < mesh.normals.size()) {
            data.push_back((float)mesh.normals[i].x);
            data.push_back((float)mesh.normals[i].y);
            data.push_back((float)mesh.normals[i].z);
        } else {
            data.insert(data.end(), {0.f, 0.f, 1.f});
        }
        if (2 * i + 1 < mesh.uvs.size()) {
            data.push_back(mesh.uvs[2 * i]);
            data.push_back(mesh.uvs[2 * i + 1]);
        } else {
            data.insert(data.end(), {0.f, 0.f});
        }
    }
    return data;
}
}  // namespace

GlMesh::~GlMesh() { destroy(); }

void GlMesh::destroy() {
    if (ebo_) glDeleteBuffers(1, &ebo_);
    if (vbo_) glDeleteBuffers(1, &vbo_);
    if (vao_) glDeleteVertexArrays(1, &vao_);
    if (tex_) glDeleteTextures(1, &tex_);
    vao_ = vbo_ = ebo_ = tex_ = 0;
    index_count_ = 0;
}

void GlMesh::move_from(GlMesh& o) {
    vao_ = o.vao_; vbo_ = o.vbo_; ebo_ = o.ebo_; tex_ = o.tex_;
    index_count_ = o.index_count_;
    o.vao_ = o.vbo_ = o.ebo_ = o.tex_ = 0;
    o.index_count_ = 0;
}

void GlMesh::upload(const CpuMesh& mesh) {
    std::vector<float> data = interleave(mesh);
    index_count_ = static_cast<GLsizei>(mesh.indices.size());

    glGenVertexArrays(1, &vao_);
    glBindVertexArray(vao_);

    glGenBuffers(1, &vbo_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, data.size() * sizeof(float), data.data(),
                 GL_STATIC_DRAW);

    glGenBuffers(1, &ebo_);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, mesh.indices.size() * sizeof(unsigned int),
                 mesh.indices.data(), GL_STATIC_DRAW);

    const GLsizei stride = 8 * sizeof(float);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride, (void*)(6 * sizeof(float)));

    glBindVertexArray(0);
}

void GlMesh::set_texture_rgba(const unsigned char* rgba, int w, int h) {
    if (!rgba || w <= 0 || h <= 0) return;
    glGenTextures(1, &tex_);
    glBindTexture(GL_TEXTURE_2D, tex_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void GlMesh::bind_texture() const {
    // Texture unit 0 is active by default and is what the sampler uniform uses.
    glBindTexture(GL_TEXTURE_2D, tex_);
}

void GlMesh::draw() const {
    glBindVertexArray(vao_);
    glDrawElements(GL_TRIANGLES, index_count_, GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);
}

GlLines::~GlLines() { destroy(); }

void GlLines::destroy() {
    if (ebo_) glDeleteBuffers(1, &ebo_);
    if (vbo_) glDeleteBuffers(1, &vbo_);
    if (vao_) glDeleteVertexArrays(1, &vao_);
    vao_ = vbo_ = ebo_ = 0;
    index_count_ = 0;
}

void GlLines::move_from(GlLines& o) {
    vao_ = o.vao_; vbo_ = o.vbo_; ebo_ = o.ebo_;
    index_count_ = o.index_count_;
    o.vao_ = o.vbo_ = o.ebo_ = 0;
    o.index_count_ = 0;
}

void GlLines::upload(const LineMesh& lines) {
    std::vector<float> data;
    data.reserve(lines.positions.size() * 3);
    for (const Vec3& p : lines.positions) {
        data.push_back((float)p.x);
        data.push_back((float)p.y);
        data.push_back((float)p.z);
    }
    index_count_ = static_cast<GLsizei>(lines.indices.size());

    glGenVertexArrays(1, &vao_);
    glBindVertexArray(vao_);
    glGenBuffers(1, &vbo_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, data.size() * sizeof(float), data.data(),
                 GL_STATIC_DRAW);
    glGenBuffers(1, &ebo_);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, lines.indices.size() * sizeof(unsigned int),
                 lines.indices.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glBindVertexArray(0);
}

void GlLines::draw() const {
    glBindVertexArray(vao_);
    glDrawElements(GL_LINES, index_count_, GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);
}

}  // namespace animate
