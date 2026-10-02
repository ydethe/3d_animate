#pragma once

#include <string>
#include <vector>

#include "types.hpp"

namespace animate {

// Parse an STL file (binary or ASCII, auto-detected) into a triangle list.
// Returns an empty vector if the file yields no triangles (spec §3.1).
std::vector<Triangle> load_stl(const std::string& path);

}  // namespace animate
