#include "stl.hpp"

#include <cstdint>
#include <cstring>
#include <fstream>
#include <sstream>

namespace animate {
namespace {

// Read the whole file into a byte buffer.
std::vector<unsigned char> read_all(const std::string& path) {
    std::ifstream fh(path, std::ios::binary);
    if (!fh) return {};
    return std::vector<unsigned char>((std::istreambuf_iterator<char>(fh)),
                                      std::istreambuf_iterator<char>());
}

float read_le_float(const unsigned char* p) {
    float f;
    std::memcpy(&f, p, 4);  // host is assumed little-endian (x86/ARM)
    return f;
}

uint32_t read_le_u32(const unsigned char* p) {
    uint32_t v;
    std::memcpy(&v, p, 4);
    return v;
}

// Heuristic binary-vs-ASCII detection (spec §3.1).
bool is_binary_stl(const std::vector<unsigned char>& data) {
    if (data.size() < 84) return false;  // too small for header + count
    uint32_t tri_count = read_le_u32(data.data() + 80);
    uint64_t expected = 84ull + static_cast<uint64_t>(tri_count) * 50ull;
    if (data.size() == expected) return true;

    // Otherwise: ASCII iff the first non-whitespace 5 chars are not "solid".
    size_t i = 0;
    while (i < data.size() &&
           std::isspace(static_cast<unsigned char>(data[i])))
        ++i;
    std::string head;
    for (size_t k = i; k < data.size() && head.size() < 5; ++k)
        head.push_back(static_cast<char>(std::tolower(data[k])));
    return head != "solid";  // binary if it does NOT start with "solid"
}

std::vector<Triangle> parse_binary(const std::vector<unsigned char>& data) {
    std::vector<Triangle> tris;
    if (data.size() < 84) return tris;
    uint32_t tri_count = read_le_u32(data.data() + 80);
    size_t off = 84;
    for (uint32_t i = 0; i < tri_count; ++i) {
        if (off + 50 > data.size()) break;  // truncated record -> stop
        const unsigned char* p = data.data() + off;
        Triangle t;
        t.normal = {read_le_float(p), read_le_float(p + 4), read_le_float(p + 8)};
        for (int v = 0; v < 3; ++v) {
            const unsigned char* q = p + 12 + v * 12;
            t.v[v] = {read_le_float(q), read_le_float(q + 4), read_le_float(q + 8)};
        }
        // 2-byte attribute field (p+48..49) ignored.
        tris.push_back(t);
        off += 50;
    }
    return tris;
}

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::vector<Triangle> parse_ascii(const std::vector<unsigned char>& data) {
    std::vector<Triangle> tris;
    Vec3 normal{0, 0, 0};
    std::vector<Vec3> verts;

    std::string text(data.begin(), data.end());
    std::istringstream stream(text);
    std::string line;
    while (std::getline(stream, line)) {
        std::istringstream ls(line);
        std::vector<std::string> parts;
        std::string tok;
        while (ls >> tok) parts.push_back(tok);
        if (parts.empty()) continue;

        std::string key = lower(parts[0]);
        if (key == "facet" && parts.size() >= 5) {
            normal = {std::strtod(parts[2].c_str(), nullptr),
                      std::strtod(parts[3].c_str(), nullptr),
                      std::strtod(parts[4].c_str(), nullptr)};
        } else if (key == "vertex" && parts.size() >= 4) {
            verts.push_back({std::strtod(parts[1].c_str(), nullptr),
                             std::strtod(parts[2].c_str(), nullptr),
                             std::strtod(parts[3].c_str(), nullptr)});
        } else if (key == "endfacet") {
            if (verts.size() == 3) {
                Triangle t;
                t.normal = normal;
                t.v[0] = verts[0];
                t.v[1] = verts[1];
                t.v[2] = verts[2];
                tris.push_back(t);
            }
            verts.clear();
        }
    }
    return tris;
}

}  // namespace

std::vector<Triangle> load_stl(const std::string& path) {
    std::vector<unsigned char> data = read_all(path);
    if (data.empty()) return {};
    return is_binary_stl(data) ? parse_binary(data) : parse_ascii(data);
}

}  // namespace animate
