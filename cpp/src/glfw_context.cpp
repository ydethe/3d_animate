#ifdef ANIMATE_BUILD_INTERACTIVE

#include "glfw_context.hpp"

#include "gl.hpp"  // GL headers first; keep GLFW from re-including them

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include "log.hpp"

namespace animate {

GlfwContext::GlfwContext(int width, int height, const char* title, bool transparent) {
    if (!glfwInit()) {
        log_error("GLFW: initialisation failed (no display?)");
        return;
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
    if (transparent) glfwWindowHint(GLFW_TRANSPARENT_FRAMEBUFFER, GLFW_TRUE);

    win_ = glfwCreateWindow(width, height, title, nullptr, nullptr);
    if (!win_) {
        log_error("GLFW: could not create a window");
        glfwTerminate();
        return;
    }
    glfwMakeContextCurrent(win_);
    glfwSwapInterval(1);

    if (!gl::load(reinterpret_cast<gl::GetProcFn>(glfwGetProcAddress))) {
        log_error("GLFW: failed to load required OpenGL entry points");
        return;
    }
    ok_ = true;
}

GlfwContext::~GlfwContext() {
    if (win_) glfwDestroyWindow(win_);
    glfwTerminate();
}

bool GlfwContext::should_close() const {
    return win_ && glfwWindowShouldClose(win_);
}

void GlfwContext::swap_and_poll() {
    glfwSwapBuffers(win_);
    glfwPollEvents();
}

}  // namespace animate

#endif  // ANIMATE_BUILD_INTERACTIVE
