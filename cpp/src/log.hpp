// Minimal leveled logging to stderr, mirroring the reference's info/warning/error
// channels. Keeps stdout clean for any machine-readable use.
#pragma once

#include <cstdio>
#include <string>
#include <utility>

namespace animate {

// Plain (non-format) overloads for literal messages — avoid -Wformat-security.
inline void log_info(const char* msg) { std::fprintf(stderr, "[info]  %s\n", msg); }
inline void log_warn(const char* msg) { std::fprintf(stderr, "[warn]  %s\n", msg); }
inline void log_error(const char* msg) { std::fprintf(stderr, "[error] %s\n", msg); }

template <typename... Args>
inline void log_info(const char* fmt, Args&&... args) {
    std::fprintf(stderr, "[info]  ");
    std::fprintf(stderr, fmt, std::forward<Args>(args)...);
    std::fprintf(stderr, "\n");
}

template <typename... Args>
inline void log_warn(const char* fmt, Args&&... args) {
    std::fprintf(stderr, "[warn]  ");
    std::fprintf(stderr, fmt, std::forward<Args>(args)...);
    std::fprintf(stderr, "\n");
}

template <typename... Args>
inline void log_error(const char* fmt, Args&&... args) {
    std::fprintf(stderr, "[error] ");
    std::fprintf(stderr, fmt, std::forward<Args>(args)...);
    std::fprintf(stderr, "\n");
}

// Report a fatal user-facing error and terminate (spec §9).
[[noreturn]] inline void fatal(const std::string& msg) {
    std::fprintf(stderr, "[error] %s\n", msg.c_str());
    std::exit(1);
}

}  // namespace animate
