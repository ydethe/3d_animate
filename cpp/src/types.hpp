// Core value types and a minimal column-major 4x4 matrix / vec3 math library.
// Right-handed, OpenGL-style (column-major, element (row r, col c) at m[c*4+r]).
#pragma once

#include <array>
#include <cmath>
#include <vector>

namespace animate {

struct Vec3 {
    double x = 0.0, y = 0.0, z = 0.0;

    Vec3() = default;
    Vec3(double x_, double y_, double z_) : x(x_), y(y_), z(z_) {}

    Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vec3 operator*(double s) const { return {x * s, y * s, z * s}; }

    double dot(const Vec3& o) const { return x * o.x + y * o.y + z * o.z; }
    Vec3 cross(const Vec3& o) const {
        return {y * o.z - z * o.y, z * o.x - x * o.z, x * o.y - y * o.x};
    }
    double length_squared() const { return x * x + y * y + z * z; }
    double length() const { return std::sqrt(length_squared()); }
    Vec3 normalized() const {
        double l = length();
        return l > 0.0 ? Vec3{x / l, y / l, z / l} : Vec3{0, 0, 0};
    }
};

// One triangle: a face normal plus three vertex positions (spec §3.1).
struct Triangle {
    Vec3 normal;
    Vec3 v[3];
};

// RGB(A) color with channels in [0, 1].
struct Color {
    float r = 0, g = 0, b = 0, a = 1;
};

// Column-major 4x4 matrix.
struct Mat4 {
    std::array<float, 16> m{};

    static Mat4 identity() {
        Mat4 r;
        r.m = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
        return r;
    }

    // this * o  (standard matrix product; applies o first then this).
    Mat4 operator*(const Mat4& o) const {
        Mat4 r;
        for (int c = 0; c < 4; ++c) {
            for (int row = 0; row < 4; ++row) {
                float s = 0;
                for (int k = 0; k < 4; ++k) s += m[k * 4 + row] * o.m[c * 4 + k];
                r.m[c * 4 + row] = s;
            }
        }
        return r;
    }

    static Mat4 translate(const Vec3& t) {
        Mat4 r = identity();
        r.m[12] = (float)t.x;
        r.m[13] = (float)t.y;
        r.m[14] = (float)t.z;
        return r;
    }

    static Mat4 scale(double s) {
        Mat4 r = identity();
        r.m[0] = r.m[5] = r.m[10] = (float)s;
        return r;
    }

    // Rotation about the Z (up) axis by `deg` degrees, counter-clockwise.
    static Mat4 rotate_z(double deg) {
        double rad = deg * M_PI / 180.0;
        float c = (float)std::cos(rad), s = (float)std::sin(rad);
        Mat4 r = identity();
        r.m[0] = c;  r.m[4] = -s;
        r.m[1] = s;  r.m[5] = c;
        return r;
    }

    // Right-handed perspective projection (fovy in degrees).
    static Mat4 perspective(double fovy_deg, double aspect, double znear, double zfar) {
        double f = 1.0 / std::tan(fovy_deg * M_PI / 360.0);
        Mat4 r;  // zero-initialised
        r.m[0] = (float)(f / aspect);
        r.m[5] = (float)f;
        r.m[10] = (float)((zfar + znear) / (znear - zfar));
        r.m[11] = -1.0f;
        r.m[14] = (float)((2.0 * zfar * znear) / (znear - zfar));
        return r;
    }

    // Right-handed look-at view matrix.
    static Mat4 look_at(const Vec3& eye, const Vec3& center, const Vec3& up) {
        Vec3 f = (center - eye).normalized();
        Vec3 s = f.cross(up).normalized();
        Vec3 u = s.cross(f);
        Mat4 r = identity();
        r.m[0] = (float)s.x; r.m[4] = (float)s.y; r.m[8] = (float)s.z;
        r.m[1] = (float)u.x; r.m[5] = (float)u.y; r.m[9] = (float)u.z;
        r.m[2] = -(float)f.x; r.m[6] = -(float)f.y; r.m[10] = -(float)f.z;
        r.m[12] = -(float)s.dot(eye);
        r.m[13] = -(float)u.dot(eye);
        r.m[14] = (float)f.dot(eye);
        return r;
    }
};

// Convert a Panda3D-style heading/pitch into a forward direction vector in a
// Z-up right-handed frame: start from +Y, pitch about X, then heading about Z.
inline Vec3 hpr_to_forward(double heading_deg, double pitch_deg) {
    double h = heading_deg * M_PI / 180.0;
    double p = pitch_deg * M_PI / 180.0;
    double cp = std::cos(p), sp = std::sin(p);
    double ch = std::cos(h), sh = std::sin(h);
    // after pitch about X: (0, cp, sp); after heading about Z (CCW):
    return Vec3{-cp * sh, cp * ch, sp}.normalized();
}

}  // namespace animate
