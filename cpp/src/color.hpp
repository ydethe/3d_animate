#pragma once

#include <string>

#include "types.hpp"

namespace animate {

// Parse a 6-digit hex RGB string (optionally '#'-prefixed) into channels in
// [0, 1] (spec §2.2). Throws std::invalid_argument on a malformed string.
Color parse_hex_color(const std::string& value);

}  // namespace animate
