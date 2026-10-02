#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "app.hpp"
#include "color.hpp"
#include "log.hpp"

namespace {

using animate::Color;

const char* kUsage =
    "Usage: 3d_animate <model> [options]\n"
    "\n"
    "Spin a 3D model in a viewer, or render one 360° revolution to video.\n"
    "\n"
    "Positional:\n"
    "  <model>               path to the model (.stl, .obj, .gltf, .glb, .dae,\n"
    "                        .fbx, .ply, .3ds)\n"
    "\n"
    "Options:\n"
    "  --speed <deg/s>       rotation speed, negative reverses   (default 45)\n"
    "  --color <hex>         base RGB color for untextured models (default ff0000)\n"
    "  --wireframe           render facet outline edges only\n"
    "  --no-wireframe        force solid rendering (default)\n"
    "  --edge-color <hex>    wireframe edge color                 (default 000000)\n"
    "  --bg-color <hex>      background color (default: transparent)\n"
    "  --output <file>       render to this video file and exit\n"
    "                        (.webm/.mov transparent, .mp4 opaque)\n"
    "  --fps <n>             frames per second for video          (default 30)\n"
    "  --width <px>          video width                          (default 1920)\n"
    "  --height <px>         video height                         (default 1080)\n"
    "  --simplify <r>        decimate STL to fraction r in (0,1]  of its faces\n"
    "  -h, --help            show this help\n";

// Fetch the value following a flag, erroring if absent.
std::string need_value(const std::string& flag, const std::string& inline_val,
                       bool has_inline, const std::vector<std::string>& argv,
                       size_t& i) {
    if (has_inline) return inline_val;
    if (i + 1 >= argv.size()) animate::fatal("Option " + flag + " requires a value.");
    return argv[++i];
}

}  // namespace

int main(int argc, char** argv) {
    std::vector<std::string> args(argv + 1, argv + argc);
    animate::Options opt;

    bool have_model = false;
    bool bg_given = false;
    std::string color_str = "ff0000", edge_str = "000000", bg_str;

    for (size_t i = 0; i < args.size(); ++i) {
        std::string a = args[i];

        // Split --opt=value form.
        std::string inline_val;
        bool has_inline = false;
        if (a.rfind("--", 0) == 0) {
            auto eq = a.find('=');
            if (eq != std::string::npos) {
                inline_val = a.substr(eq + 1);
                has_inline = true;
                a = a.substr(0, eq);
            }
        }

        if (a == "-h" || a == "--help") {
            std::cout << kUsage;
            return 0;
        } else if (a == "--wireframe") {
            opt.wireframe = true;
        } else if (a == "--no-wireframe") {
            opt.wireframe = false;
        } else if (a == "--speed") {
            opt.speed = std::strtod(
                need_value(a, inline_val, has_inline, args, i).c_str(), nullptr);
        } else if (a == "--color") {
            color_str = need_value(a, inline_val, has_inline, args, i);
        } else if (a == "--edge-color") {
            edge_str = need_value(a, inline_val, has_inline, args, i);
        } else if (a == "--bg-color") {
            bg_str = need_value(a, inline_val, has_inline, args, i);
            bg_given = true;
        } else if (a == "--output") {
            opt.output = need_value(a, inline_val, has_inline, args, i);
            opt.has_output = true;
        } else if (a == "--fps") {
            opt.fps = std::atoi(need_value(a, inline_val, has_inline, args, i).c_str());
        } else if (a == "--width") {
            opt.width = std::atoi(need_value(a, inline_val, has_inline, args, i).c_str());
        } else if (a == "--height") {
            opt.height = std::atoi(need_value(a, inline_val, has_inline, args, i).c_str());
        } else if (a == "--simplify") {
            opt.simplify = std::strtod(
                need_value(a, inline_val, has_inline, args, i).c_str(), nullptr);
            opt.has_simplify = true;
        } else if (a.rfind("-", 0) == 0) {
            animate::fatal("Unknown option: " + a);
        } else if (!have_model) {
            opt.model = args[i];
            have_model = true;
        } else {
            animate::fatal("Unexpected extra argument: " + args[i]);
        }
    }

    if (!have_model) {
        std::cerr << kUsage;
        return 2;
    }

    // Validation (spec §2.3).
    if (opt.has_simplify && !(opt.simplify > 0.0 && opt.simplify <= 1.0)) {
        animate::fatal("--simplify must be in the range (0, 1]");
    }

    try {
        opt.color = animate::parse_hex_color(color_str);
        opt.edge_color = animate::parse_hex_color(edge_str);
        if (bg_given) {
            opt.background = animate::parse_hex_color(bg_str);
            opt.background.a = 1.0f;
        } else {
            opt.background = Color{0, 0, 0, 0};  // transparent
        }
    } catch (const std::exception& e) {
        animate::fatal(e.what());
    }

    // Create the output's parent directory if needed (spec §2.3).
    if (opt.has_output) {
        std::filesystem::path parent = std::filesystem::path(opt.output).parent_path();
        if (!parent.empty() && !std::filesystem::exists(parent)) {
            std::error_code ec;
            std::filesystem::create_directories(parent, ec);
        }
    }

    return animate::run(opt);
}
