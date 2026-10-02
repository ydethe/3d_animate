#include "gl.hpp"

#include "log.hpp"

namespace animate {
namespace gl {

#define ANIMATE_GL_DEFINE(type, name) type name = nullptr;
ANIMATE_GL_FUNCS(ANIMATE_GL_DEFINE)
#undef ANIMATE_GL_DEFINE

bool load(GetProcFn getproc) {
    bool ok = true;
#define ANIMATE_GL_LOAD(type, name)                                   \
    name = reinterpret_cast<type>(getproc(#name));                    \
    if (!name) {                                                      \
        log_warn("OpenGL: could not load %s", #name);                \
        ok = false;                                                   \
    }
    ANIMATE_GL_FUNCS(ANIMATE_GL_LOAD)
#undef ANIMATE_GL_LOAD
    return ok;
}

}  // namespace gl
}  // namespace animate
