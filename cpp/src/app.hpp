#pragma once

#include <string>

#include "types.hpp"

namespace animate {

// Fully-parsed, validated invocation options (spec §2).
struct Options {
    std::string model;
    double speed = 45.0;
    Color color;        // base tint for untextured models
    bool wireframe = false;
    Color edge_color;
    Color background;   // alpha 0 means transparent / no bg
    bool has_output = false;
    std::string output;
    int fps = 30;
    int width = 1920;
    int height = 1080;
    bool has_simplify = false;
    double simplify = 1.0;
};

// Load, build, and either export a video or open the interactive viewer.
int run(const Options& opt);

}  // namespace animate
