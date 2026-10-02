#include "egl_context.hpp"

#include <EGL/egl.h>
#include <EGL/eglext.h>

#include <algorithm>
#include <cstring>

#include "gl.hpp"
#include "log.hpp"

namespace animate {

namespace {

// Obtain an EGLDisplay that works on a headless server (no X/Wayland).
//
// The classic trap is eglGetDisplay(EGL_DEFAULT_DISPLAY): with no $DISPLAY,
// Mesa resolves it to the X11 platform and eglInitialize then fails even when a
// software GL driver is installed. We instead try, in order:
//   1. the EGL device platform (enumerates Mesa's software device; supports
//      pbuffer surfaces, so the rest of the setup is unchanged);
//   2. the surfaceless platform (render only to FBOs, no pbuffer);
//   3. the legacy default display (for machines that do have X11/Wayland).
EGLDisplay open_headless_display(bool& surfaceless_out) {
    surfaceless_out = false;
    const char* client_exts = eglQueryString(EGL_NO_DISPLAY, EGL_EXTENSIONS);
    auto has_ext = [&](const char* name) {
        return client_exts != nullptr && std::strstr(client_exts, name) != nullptr;
    };
    auto get_platform_display = reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(
        eglGetProcAddress("eglGetPlatformDisplayEXT"));

    // 1. EGL device platform (incl. Mesa's software device).
    if (get_platform_display && has_ext("EGL_EXT_platform_device") &&
        has_ext("EGL_EXT_device_enumeration")) {
        auto query_devices = reinterpret_cast<PFNEGLQUERYDEVICESEXTPROC>(
            eglGetProcAddress("eglQueryDevicesEXT"));
        EGLDeviceEXT devices[16];
        EGLint num_devices = 0;
        if (query_devices && query_devices(16, devices, &num_devices)) {
            for (EGLint i = 0; i < num_devices; ++i) {
                EGLDisplay d = get_platform_display(EGL_PLATFORM_DEVICE_EXT,
                                                    devices[i], nullptr);
                EGLint major = 0, minor = 0;
                if (d != EGL_NO_DISPLAY && eglInitialize(d, &major, &minor))
                    return d;
            }
        }
    }

    // 2. Surfaceless platform.
    if (get_platform_display && has_ext("EGL_MESA_platform_surfaceless")) {
        EGLDisplay d = get_platform_display(EGL_PLATFORM_SURFACELESS_MESA,
                                            reinterpret_cast<void*>(EGL_DEFAULT_DISPLAY),
                                            nullptr);
        EGLint major = 0, minor = 0;
        if (d != EGL_NO_DISPLAY && eglInitialize(d, &major, &minor)) {
            surfaceless_out = true;
            return d;
        }
    }

    // 3. Legacy default display.
    EGLDisplay d = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    EGLint major = 0, minor = 0;
    if (d != EGL_NO_DISPLAY && eglInitialize(d, &major, &minor)) return d;

    return EGL_NO_DISPLAY;
}

}  // namespace

EglContext::EglContext(int width, int height) : width_(width), height_(height) {
    bool surfaceless = false;
    EGLDisplay display = open_headless_display(surfaceless);
    if (display == EGL_NO_DISPLAY) {
        log_error(
            "EGL: could not open a headless display (need EGL + a GL driver; on a "
            "headless server install a software GL driver and set "
            "LIBGL_ALWAYS_SOFTWARE=1 — see cpp/README.md)");
        return;
    }
    display_ = display;
    if (!eglBindAPI(EGL_OPENGL_API)) {
        log_error("EGL: could not bind the desktop OpenGL API");
        return;
    }

    // A pbuffer-capable config when we have a real surface; the surfaceless
    // platform exposes no window/pbuffer surfaces, so don't require one there.
    const EGLint cfg_attribs[] = {
        EGL_SURFACE_TYPE, surfaceless ? EGL_DONT_CARE : EGL_PBUFFER_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
        EGL_DEPTH_SIZE, 24,
        EGL_NONE};
    EGLConfig config;
    EGLint num_config = 0;
    if (!eglChooseConfig(static_cast<EGLDisplay>(display_), cfg_attribs, &config, 1,
                         &num_config) ||
        num_config == 0) {
        log_error("EGL: no suitable framebuffer config");
        return;
    }

    if (!surfaceless) {
        const EGLint pbuf_attribs[] = {EGL_WIDTH, width, EGL_HEIGHT, height, EGL_NONE};
        surface_ = eglCreatePbufferSurface(static_cast<EGLDisplay>(display_), config,
                                           pbuf_attribs);
        if (surface_ == EGL_NO_SURFACE) {
            log_error("EGL: could not create pbuffer surface");
            return;
        }
    }

    const EGLint ctx_attribs[] = {
        EGL_CONTEXT_MAJOR_VERSION, 3, EGL_CONTEXT_MINOR_VERSION, 3,
        EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,
        EGL_NONE};
    context_ = eglCreateContext(static_cast<EGLDisplay>(display_), config,
                                EGL_NO_CONTEXT, ctx_attribs);
    if (context_ == EGL_NO_CONTEXT) {
        log_error("EGL: could not create an OpenGL 3.3 core context");
        return;
    }
    // On the surfaceless platform we bind the context with no draw/read surface
    // (EGL_KHR_surfaceless_context) and render straight into our capture FBO.
    EGLSurface draw = static_cast<EGLSurface>(surface_);  // EGL_NO_SURFACE if surfaceless
    if (!eglMakeCurrent(static_cast<EGLDisplay>(display_), draw, draw,
                        static_cast<EGLContext>(context_))) {
        log_error("EGL: eglMakeCurrent failed");
        return;
    }

    if (!gl::load(reinterpret_cast<gl::GetProcFn>(eglGetProcAddress))) {
        log_error("EGL: failed to load required OpenGL entry points");
        return;
    }

    // Dedicated capture FBO: RGBA8 color texture + depth24 renderbuffer.
    glGenTextures(1, &color_tex_);
    glBindTexture(GL_TEXTURE_2D, color_tex_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    glGenRenderbuffers(1, &depth_rb_);
    glBindRenderbuffer(GL_RENDERBUFFER, depth_rb_);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width, height);

    glGenFramebuffers(1, &fbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                           color_tex_, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER,
                              depth_rb_);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        log_error("EGL: capture framebuffer is incomplete");
        return;
    }

    alpha_bits_ = 8;  // our FBO always carries alpha
    ok_ = true;
}

EglContext::~EglContext() {
    if (display_) {
        if (fbo_) glDeleteFramebuffers(1, &fbo_);
        if (depth_rb_) glDeleteRenderbuffers(1, &depth_rb_);
        if (color_tex_) glDeleteTextures(1, &color_tex_);
        eglMakeCurrent(static_cast<EGLDisplay>(display_), EGL_NO_SURFACE,
                       EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (context_)
            eglDestroyContext(static_cast<EGLDisplay>(display_),
                              static_cast<EGLContext>(context_));
        if (surface_)
            eglDestroySurface(static_cast<EGLDisplay>(display_),
                              static_cast<EGLSurface>(surface_));
        eglTerminate(static_cast<EGLDisplay>(display_));
    }
}

void EglContext::bind() {
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glViewport(0, 0, width_, height_);
}

void EglContext::read_rgba(std::vector<unsigned char>& out) {
    out.resize(static_cast<size_t>(width_) * height_ * 4);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, width_, height_, GL_RGBA, GL_UNSIGNED_BYTE, out.data());

    // glReadPixels yields bottom-up rows; flip to top-down image order.
    const size_t row = static_cast<size_t>(width_) * 4;
    std::vector<unsigned char> tmp(row);
    for (int y = 0; y < height_ / 2; ++y) {
        unsigned char* a = out.data() + static_cast<size_t>(y) * row;
        unsigned char* b = out.data() + static_cast<size_t>(height_ - 1 - y) * row;
        std::copy(a, a + row, tmp.data());
        std::copy(b, b + row, a);
        std::copy(tmp.data(), tmp.data() + row, b);
    }
}

}  // namespace animate
