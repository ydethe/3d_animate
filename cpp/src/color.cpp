#include "color.hpp"

#include <cctype>
#include <stdexcept>

namespace animate {

Color parse_hex_color(const std::string& value) {
    // Strip a single optional leading '#'.
    std::string hex = value;
    if (!hex.empty() && hex.front() == '#') hex.erase(hex.begin());

    if (hex.size() != 6) {
        throw std::invalid_argument(
            "Color must be a 6-digit hex string, e.g. ff6600 or #ff6600");
    }
    for (char c : hex) {
        if (!std::isxdigit(static_cast<unsigned char>(c))) {
            throw std::invalid_argument("Invalid hex color: " + value);
        }
    }

    auto byte = [&](int i) {
        return static_cast<int>(std::stoi(hex.substr(i, 2), nullptr, 16));
    };
    Color c;
    c.r = byte(0) / 255.0f;
    c.g = byte(2) / 255.0f;
    c.b = byte(4) / 255.0f;
    c.a = 1.0f;
    return c;
}

}  // namespace animate
