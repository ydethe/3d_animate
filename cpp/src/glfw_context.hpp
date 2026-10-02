#pragma once

#ifdef ANIMATE_BUILD_INTERACTIVE

struct GLFWwindow;

namespace animate {

// Interactive OpenGL 3.3-core window (spec §7). Makes itself current and loads
// GL entry points on construction.
class GlfwContext {
public:
    GlfwContext(int width, int height, const char* title, bool transparent);
    ~GlfwContext();
    GlfwContext(const GlfwContext&) = delete;
    GlfwContext& operator=(const GlfwContext&) = delete;

    bool ok() const { return ok_; }
    GLFWwindow* window() const { return win_; }
    bool should_close() const;
    void swap_and_poll();

private:
    GLFWwindow* win_ = nullptr;
    bool ok_ = false;
};

}  // namespace animate

#endif  // ANIMATE_BUILD_INTERACTIVE
