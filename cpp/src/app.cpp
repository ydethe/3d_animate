#include "app.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <vector>

#include "assimp_load.hpp"
#include "egl_context.hpp"
#include "exporter.hpp"
#include "gl.hpp"
#include "glmesh.hpp"
#include "log.hpp"
#include "mesh.hpp"
#include "renderer.hpp"
#include "stl.hpp"

#ifdef ANIMATE_BUILD_INTERACTIVE
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include "glfw_context.hpp"
#endif

namespace animate {
namespace {

namespace fs = std::filesystem;

std::string lower_ext(const std::string& path) {
    std::string ext = fs::path(path).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return ext;
}

// Axis-aligned bounds over a list of positions.
void bounds_of(const std::vector<Vec3>& pts, Vec3& mn, Vec3& mx, bool& first) {
    for (const Vec3& p : pts) {
        if (first) {
            mn = mx = p;
            first = false;
        } else {
            mn.x = std::min(mn.x, p.x); mn.y = std::min(mn.y, p.y); mn.z = std::min(mn.z, p.z);
            mx.x = std::max(mx.x, p.x); mx.y = std::max(mx.y, p.y); mx.z = std::max(mx.z, p.z);
        }
    }
}

// CPU geometry prepared for upload.
struct Geometry {
    bool wireframe = false;
    LineMesh outline;                 // wireframe
    std::vector<CpuMesh> solids;      // solid
    std::vector<Texture> textures;    // parallel to solids (may be empty)
    Vec3 min_b, max_b;
};

Geometry build_geometry(const Options& opt) {
    Geometry g;
    g.wireframe = opt.wireframe;
    const bool is_stl = lower_ext(opt.model) == ".stl";
    bool first = true;

    if (is_stl) {
        std::vector<Triangle> tris = load_stl(opt.model);
        if (tris.empty()) {
            fatal("No triangles found in '" + opt.model + "' — is it a valid STL?");
        }
        log_info("Loaded %zu triangles from '%s'", tris.size(), opt.model.c_str());

        if (opt.has_simplify) tris = simplify_stl(tris, opt.simplify);

        if (opt.wireframe) {
            g.outline = build_facet_outline(tris);
            bounds_of(g.outline.positions, g.min_b, g.max_b, first);
        } else {
            CpuMesh solid = stl_to_solid(tris);
            bounds_of(solid.positions, g.min_b, g.max_b, first);
            g.solids.push_back(std::move(solid));
            g.textures.emplace_back();  // no texture
        }
    } else {
        AssimpModel model = load_assimp(opt.model);
        if (opt.wireframe) {
            log_info("Extracted %zu triangles from %s for wireframe",
                     model.triangles.size(), opt.model.c_str());
            g.outline = build_facet_outline(model.triangles);
            bounds_of(g.outline.positions, g.min_b, g.max_b, first);
        } else {
            // Move solids + textures over and accumulate bounds.
            for (size_t i = 0; i < model.meshes.size(); ++i) {
                bounds_of(model.meshes[i].positions, g.min_b, g.max_b, first);
                g.solids.push_back(std::move(model.meshes[i]));
                g.textures.push_back(std::move(model.textures[i]));
            }
            if (!model.has_texture) {
                log_info("No textures found; tinting with base color");
            }
        }
    }

    if (opt.wireframe && g.outline.indices.empty()) {
        fatal("--wireframe could not obtain mesh geometry for this file.");
    }
    return g;
}

void upload_to_renderer(Renderer& renderer, Geometry& g) {
    if (g.wireframe) {
        GlLines lines;
        lines.upload(g.outline);
        renderer.set_lines(std::move(lines));
    } else {
        std::vector<GlMesh> meshes;
        meshes.reserve(g.solids.size());
        for (size_t i = 0; i < g.solids.size(); ++i) {
            GlMesh m;
            m.upload(g.solids[i]);
            if (i < g.textures.size() && g.textures[i].valid) {
                m.set_texture_rgba(g.textures[i].rgba.data(), g.textures[i].w,
                                   g.textures[i].h);
            }
            meshes.push_back(std::move(m));
        }
        renderer.set_solid(std::move(meshes));
    }
    renderer.set_bounds(g.min_b, g.max_b);
}

SceneConfig make_scene_config(const Options& opt) {
    SceneConfig cfg;
    cfg.wireframe = opt.wireframe;
    cfg.base_color = opt.color;
    cfg.edge_color = opt.edge_color;
    cfg.background = opt.background;
    cfg.width = opt.width;
    cfg.height = opt.height;
    return cfg;
}

int run_export_mode(const Options& opt, Geometry& g) {
    EglContext ctx(opt.width, opt.height);
    if (!ctx.ok()) {
        fatal("Could not create an offscreen OpenGL context for export. On a "
              "headless server install EGL + a software GL driver and set "
              "LIBGL_ALWAYS_SOFTWARE=1 (see cpp/README.md).");
    }
    if (!ctx.has_alpha()) {
        log_warn("Capture buffer has no alpha channel; output will be opaque.");
    }

    SceneConfig cfg = make_scene_config(opt);
    Renderer renderer(cfg);
    upload_to_renderer(renderer, g);

    ExportConfig ecfg;
    ecfg.output_path = opt.output;
    ecfg.speed = opt.speed;
    ecfg.fps = opt.fps;
    ecfg.width = opt.width;
    ecfg.height = opt.height;
    run_export(renderer, ctx, ecfg);
    return 0;
}

#ifdef ANIMATE_BUILD_INTERACTIVE
struct KeyState {
    double speed = 45.0;
    bool paused = false;
};

void key_callback(GLFWwindow* win, int key, int, int action, int) {
    if (action != GLFW_PRESS) return;
    auto* ks = static_cast<KeyState*>(glfwGetWindowUserPointer(win));
    switch (key) {
        case GLFW_KEY_ESCAPE:
            glfwSetWindowShouldClose(win, GLFW_TRUE);
            break;
        case GLFW_KEY_SPACE:
            ks->paused = !ks->paused;
            log_info("%s", ks->paused ? "Paused" : "Resumed");
            break;
        case GLFW_KEY_EQUAL:   // unshifted '+'
        case GLFW_KEY_KP_ADD:
            ks->speed += 15;
            log_info("Speed: %.0f deg/s", ks->speed);
            break;
        case GLFW_KEY_MINUS:
        case GLFW_KEY_KP_SUBTRACT:
            ks->speed -= 15;
            log_info("Speed: %.0f deg/s", ks->speed);
            break;
        default:
            break;
    }
}

int run_interactive_mode(const Options& opt, Geometry& g) {
    bool transparent = opt.background.a < 1.0f;
    GlfwContext ctx(1280, 720, "3d_animate", transparent);
    if (!ctx.ok()) {
        fatal("Could not open an interactive window (no display?). Use --output "
              "to render offscreen instead.");
    }

    SceneConfig cfg = make_scene_config(opt);
    cfg.width = 1280;
    cfg.height = 720;
    Renderer renderer(cfg);
    upload_to_renderer(renderer, g);

    KeyState ks;
    ks.speed = opt.speed;
    glfwSetWindowUserPointer(ctx.window(), &ks);
    glfwSetKeyCallback(ctx.window(), key_callback);

    log_info("Controls:  +/-  speed    space  pause    esc  quit");
    log_info("Rotation speed: %.0f deg/s", ks.speed);

    double heading = 0.0;
    double last = glfwGetTime();
    while (!ctx.should_close()) {
        double now = glfwGetTime();
        double dt = now - last;
        last = now;
        if (!ks.paused) heading += ks.speed * dt;
        renderer.render_frame(heading);
        ctx.swap_and_poll();
    }
    return 0;
}
#endif  // ANIMATE_BUILD_INTERACTIVE

}  // namespace

int run(const Options& opt) {
    Geometry g = build_geometry(opt);

    if (opt.has_output) {
        return run_export_mode(opt, g);
    }
#ifdef ANIMATE_BUILD_INTERACTIVE
    return run_interactive_mode(opt, g);
#else
    fatal("Interactive mode was not built (configure with -DBUILD_INTERACTIVE=ON)."
          " Pass --output to render a video instead.");
#endif
}

}  // namespace animate
