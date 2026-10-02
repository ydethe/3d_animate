#pragma once

#include <vector>

namespace animate {

// Headless OpenGL context (EGL, surfaceless/pbuffer) plus a dedicated RGBA8 +
// depth24 framebuffer object, guaranteeing an alpha channel for transparent
// export regardless of the host visual (spec §8.2). Makes itself current and
// loads GL entry points on construction.
class EglContext {
public:
    EglContext(int width, int height);
    ~EglContext();
    EglContext(const EglContext&) = delete;
    EglContext& operator=(const EglContext&) = delete;

    bool ok() const { return ok_; }
    bool has_alpha() const { return alpha_bits_ > 0; }

    // Bind the capture FBO and set the viewport.
    void bind();
    // Read the FBO color buffer as top-down RGBA8 (width*height*4 bytes).
    void read_rgba(std::vector<unsigned char>& out);

private:
    int width_ = 0, height_ = 0;
    int alpha_bits_ = 0;
    bool ok_ = false;

    void* display_ = nullptr;   // EGLDisplay
    void* context_ = nullptr;   // EGLContext
    void* surface_ = nullptr;   // EGLSurface (pbuffer)
    unsigned fbo_ = 0, color_tex_ = 0, depth_rb_ = 0;
};

}  // namespace animate
