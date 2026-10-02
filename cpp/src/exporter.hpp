#pragma once

#include <string>

#include "egl_context.hpp"
#include "renderer.hpp"

namespace animate {

struct ExportConfig {
    std::string output_path;
    double speed = 45.0;
    int fps = 30;
    int width = 1920;
    int height = 1080;
};

// Render exactly one 360° revolution offscreen, save PNG frames, and encode them
// with ffmpeg (spec §8). Terminates the program on encoder failure.
void run_export(Renderer& renderer, EglContext& ctx, const ExportConfig& cfg);

}  // namespace animate
