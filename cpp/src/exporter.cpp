#include "exporter.hpp"

#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <random>
#include <string>
#include <vector>

#include "gl.hpp"
#include "log.hpp"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

namespace animate {
namespace {

namespace fs = std::filesystem;

std::string lower_ext(const std::string& path) {
    std::string ext = fs::path(path).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return ext;
}

// Codec/pixel-format arguments selected by the output extension (spec §8.4).
std::vector<std::string> codec_args(const std::string& ext) {
    if (ext == ".webm")
        return {"-c:v", "libvpx-vp9", "-pix_fmt", "yuva420p", "-b:v", "0", "-crf", "30"};
    if (ext == ".mov")
        return {"-c:v", "prores_ks", "-profile:v", "4", "-pix_fmt", "yuva444p10le"};
    if (ext == ".mp4")
        log_warn("H.264 inside MP4 does not support alpha."
                 " Use .webm or .mov for a transparent output.");
    return {"-c:v", "libx264", "-pix_fmt", "yuv420p", "-crf", "18"};
}

// Run a program with argv, capturing its stderr. Returns the exit code (or -1).
int run_process(const std::vector<std::string>& argv, std::string& err_out) {
    int pipefd[2];
    if (pipe(pipefd) != 0) return -1;

    pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        return -1;
    }
    if (pid == 0) {
        // Child: route stderr to the pipe, exec.
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[0]);
        close(pipefd[1]);
        std::vector<char*> c;
        c.reserve(argv.size() + 1);
        for (const auto& s : argv) c.push_back(const_cast<char*>(s.c_str()));
        c.push_back(nullptr);
        execvp(c[0], c.data());
        _exit(127);  // exec failed
    }
    // Parent: drain stderr, then reap.
    close(pipefd[1]);
    char buf[4096];
    ssize_t n;
    while ((n = read(pipefd[0], buf, sizeof(buf))) > 0) err_out.append(buf, n);
    close(pipefd[0]);
    int status = 0;
    waitpid(pid, &status, 0);
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

std::string make_temp_dir() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<int> dist(0, 0xFFFFFF);
    fs::path dir = fs::temp_directory_path() /
                   ("stl_video_" + std::to_string(dist(gen)));
    fs::create_directories(dir);
    return dir.string();
}

}  // namespace

void run_export(Renderer& renderer, EglContext& ctx, const ExportConfig& cfg) {
    double duration = 360.0 / std::abs(cfg.speed);
    int total = std::max(1, static_cast<int>(std::lround(duration * cfg.fps)));
    std::string temp_dir = make_temp_dir();

    log_info("Rendering %d frames at %d fps (%.2fs for 360°)…", total, cfg.fps, duration);

    std::vector<unsigned char> frame;
    auto start = std::chrono::steady_clock::now();

    for (int i = 0; i < total; ++i) {
        double heading = i * cfg.speed * (1.0 / cfg.fps);
        ctx.bind();
        renderer.render_frame(heading);
        glFinish();
        ctx.read_rgba(frame);

        char name[64];
        std::snprintf(name, sizeof(name), "frame_%06d.png", i);
        fs::path frame_path = fs::path(temp_dir) / name;
        if (!stbi_write_png(frame_path.string().c_str(), cfg.width, cfg.height, 4,
                            frame.data(), cfg.width * 4)) {
            fs::remove_all(temp_dir);
            fatal("Failed to write frame image " + frame_path.string());
        }

        if (i == 0 || (i + 1) % 10 == 0 || i + 1 == total) {
            auto now = std::chrono::steady_clock::now();
            double elapsed = std::chrono::duration<double>(now - start).count();
            double eta = (i > 0) ? elapsed / (i + 1) * (total - i - 1) : 0.0;
            std::fprintf(stderr, "\rCapturing %d/%d frames (ETA %.0fs)   ", i + 1,
                         total, eta);
            std::fflush(stderr);
        }
    }
    std::fprintf(stderr, "\n");

    // Encode with ffmpeg.
    std::string ext = lower_ext(cfg.output_path);
    fs::path pattern = fs::path(temp_dir) / "frame_%06d.png";
    std::vector<std::string> argv = {
        "ffmpeg", "-y", "-hide_banner", "-loglevel", "error", "-nostats",
        "-framerate", std::to_string(cfg.fps), "-i", pattern.string()};
    for (const auto& a : codec_args(ext)) argv.push_back(a);
    argv.push_back(cfg.output_path);

    log_info("Encoding %s…", cfg.output_path.c_str());
    std::string err;
    int rc = run_process(argv, err);
    if (rc != 0) {
        log_error("ffmpeg failed (exit %d):\n%s", rc, err.c_str());
        fs::remove_all(temp_dir);
        fatal("Video encoding failed.");
    }

    log_info("Saved %s", cfg.output_path.c_str());
    fs::remove_all(temp_dir);
}

}  // namespace animate
