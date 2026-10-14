/*
 * SPDX-FileCopyrightText: 2026 Atherta
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Small, dependency-free vector / quaternion / matrix types used by the
 * Atherta core. Conventions follow Blender so that ported navigation code
 * reads the same:
 *  - Quaternions are stored (w, x, y, z) and multiplied Hamilton-style.
 *  - Matrices are column-major (OpenGL layout): m[col * 4 + row].
 *  - World space is right-handed, Z-up.
 */

#pragma once

#include <algorithm>
#include <cmath>

namespace atherta {

constexpr float kPi = 3.14159265358979323846f;

inline constexpr float deg2rad(float d) { return d * (kPi / 180.0f); }
inline constexpr float rad2deg(float r) { return r * (180.0f / kPi); }

struct Vec2 {
    float x = 0.0f, y = 0.0f;

    constexpr Vec2() = default;
    constexpr Vec2(float x_, float y_) : x(x_), y(y_) {}

    constexpr Vec2 operator+(const Vec2 &o) const { return {x + o.x, y + o.y}; }
    constexpr Vec2 operator-(const Vec2 &o) const { return {x - o.x, y - o.y}; }
    constexpr Vec2 operator*(float s) const { return {x * s, y * s}; }
    constexpr Vec2 operator/(float s) const { return {x / s, y / s}; }
    float length() const { return std::sqrt(x * x + y * y); }
};

struct Vec3 {
    float x = 0.0f, y = 0.0f, z = 0.0f;

    constexpr Vec3() = default;
    constexpr Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}

    constexpr Vec3 operator+(const Vec3 &o) const { return {x + o.x, y + o.y, z + o.z}; }
    constexpr Vec3 operator-(const Vec3 &o) const { return {x - o.x, y - o.y, z - o.z}; }
    constexpr Vec3 operator-() const { return {-x, -y, -z}; }
    constexpr Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
    constexpr Vec3 operator/(float s) const { return {x / s, y / s, z / s}; }
    Vec3 &operator+=(const Vec3 &o) { x += o.x; y += o.y; z += o.z; return *this; }
    Vec3 &operator-=(const Vec3 &o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
    Vec3 &operator*=(float s) { x *= s; y *= s; z *= s; return *this; }

    float operator[](int i) const { return i == 0 ? x : (i == 1 ? y : z); }
    float &operator[](int i) { return i == 0 ? x : (i == 1 ? y : z); }

    float lengthSquared() const { return x * x + y * y + z * z; }
    float length() const { return std::sqrt(lengthSquared()); }
};

inline constexpr float dot(const Vec3 &a, const Vec3 &b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

inline constexpr Vec3 cross(const Vec3 &a, const Vec3 &b)
{
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

inline Vec3 normalized(const Vec3 &v)
{
    const float len = v.length();
    return len > 0.0f ? v / len : Vec3();
}

inline constexpr Vec3 lerp(const Vec3 &a, const Vec3 &b, float t) { return a + (b - a) * t; }

inline Vec3 componentMin(const Vec3 &a, const Vec3 &b)
{
    return {std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z)};
}

inline Vec3 componentMax(const Vec3 &a, const Vec3 &b)
{
    return {std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z)};
}

/** Unsigned angle between two normalized vectors (Blender: angle_normalized_v3v3). */
inline float angleNormalized(const Vec3 &a, const Vec3 &b)
{
    // Same numerically stable formulation as Blender.
    if (dot(a, b) >= 0.0f) {
        return 2.0f * std::asin(std::min(1.0f, (b - a).length() / 2.0f));
    }
    return kPi - 2.0f * std::asin(std::min(1.0f, (-b - a).length() / 2.0f));
}

struct Vec4 {
    float x = 0.0f, y = 0.0f, z = 0.0f, w = 0.0f;

    constexpr Vec4() = default;
    constexpr Vec4(float x_, float y_, float z_, float w_) : x(x_), y(y_), z(z_), w(w_) {}
    constexpr Vec4(const Vec3 &v, float w_) : x(v.x), y(v.y), z(v.z), w(w_) {}
    constexpr Vec3 xyz() const { return {x, y, z}; }
};

struct Quat {
    float w = 1.0f, x = 0.0f, y = 0.0f, z = 0.0f;

    constexpr Quat() = default;
    constexpr Quat(float w_, float x_, float y_, float z_) : w(w_), x(x_), y(y_), z(z_) {}

    static Quat fromAxisAngle(const Vec3 &axis, float angle)
    {
        const Vec3 n = atherta::normalized(axis);
        if (n.lengthSquared() == 0.0f) {
            return Quat();
        }
        const float s = std::sin(angle * 0.5f);
        return {std::cos(angle * 0.5f), n.x * s, n.y * s, n.z * s};
    }

    constexpr Quat operator*(const Quat &b) const
    {
        return {w * b.w - x * b.x - y * b.y - z * b.z,
                w * b.x + x * b.w + y * b.z - z * b.y,
                w * b.y + y * b.w + z * b.x - x * b.z,
                w * b.z + z * b.w + x * b.y - y * b.x};
    }

    constexpr Quat conjugated() const { return {w, -x, -y, -z}; }
    constexpr Quat operator-() const { return {-w, -x, -y, -z}; }

    float length() const { return std::sqrt(w * w + x * x + y * y + z * z); }

    Quat normalized() const
    {
        const float len = length();
        return len > 0.0f ? Quat(w / len, x / len, y / len, z / len) : Quat();
    }

    /** Rotate a vector (Blender: mul_qt_v3). */
    Vec3 rotate(const Vec3 &v) const
    {
        const Vec3 u(x, y, z);
        const Vec3 t = cross(u, v) * 2.0f;
        return v + t * w + cross(u, t);
    }
};

inline constexpr float dot(const Quat &a, const Quat &b)
{
    return a.w * b.w + a.x * b.x + a.y * b.y + a.z * b.z;
}

/** Spherical interpolation along the shortest arc (Blender: interp_qt_qtqt). */
inline Quat slerp(const Quat &a, const Quat &b, float t)
{
    float cosom = dot(a, b);
    Quat q1 = a;
    if (cosom < 0.0f) {
        cosom = -cosom;
        q1 = -q1;
    }
    float sc1, sc2;
    if ((1.0f - cosom) > 0.0001f) {
        const float omega = std::acos(cosom);
        const float sinom = std::sin(omega);
        sc1 = std::sin((1.0f - t) * omega) / sinom;
        sc2 = std::sin(t * omega) / sinom;
    } else {
        sc1 = 1.0f - t;
        sc2 = t;
    }
    return {sc1 * q1.w + sc2 * b.w, sc1 * q1.x + sc2 * b.x, sc1 * q1.y + sc2 * b.y,
            sc1 * q1.z + sc2 * b.z};
}

/** Signed-free angle of the rotation that takes @p a to @p b, in [0, pi]. */
inline float angleBetween(const Quat &a, const Quat &b)
{
    const float d = std::min(1.0f, std::fabs(dot(a.normalized(), b.normalized())));
    return 2.0f * std::acos(d);
}

struct Mat4 {
    float m[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

    static Mat4 identity() { return Mat4(); }

    float &at(int row, int col) { return m[col * 4 + row]; }
    float at(int row, int col) const { return m[col * 4 + row]; }

    static Mat4 fromQuat(const Quat &q)
    {
        Mat4 r;
        const float xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z;
        const float xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z;
        const float wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;
        r.at(0, 0) = 1 - 2 * (yy + zz);
        r.at(1, 0) = 2 * (xy + wz);
        r.at(2, 0) = 2 * (xz - wy);
        r.at(0, 1) = 2 * (xy - wz);
        r.at(1, 1) = 1 - 2 * (xx + zz);
        r.at(2, 1) = 2 * (yz + wx);
        r.at(0, 2) = 2 * (xz + wy);
        r.at(1, 2) = 2 * (yz - wx);
        r.at(2, 2) = 1 - 2 * (xx + yy);
        return r;
    }

    static Mat4 translation(const Vec3 &t)
    {
        Mat4 r;
        r.at(0, 3) = t.x;
        r.at(1, 3) = t.y;
        r.at(2, 3) = t.z;
        return r;
    }

    /** OpenGL style frustum (glFrustum). */
    static Mat4 frustum(float left, float right, float bottom, float top, float nearZ, float farZ)
    {
        Mat4 r;
        r.at(0, 0) = 2 * nearZ / (right - left);
        r.at(1, 1) = 2 * nearZ / (top - bottom);
        r.at(0, 2) = (right + left) / (right - left);
        r.at(1, 2) = (top + bottom) / (top - bottom);
        r.at(2, 2) = -(farZ + nearZ) / (farZ - nearZ);
        r.at(3, 2) = -1;
        r.at(2, 3) = -2 * farZ * nearZ / (farZ - nearZ);
        r.at(3, 3) = 0;
        return r;
    }

    /** OpenGL style orthographic projection (glOrtho). */
    static Mat4 ortho(float left, float right, float bottom, float top, float nearZ, float farZ)
    {
        Mat4 r;
        r.at(0, 0) = 2 / (right - left);
        r.at(1, 1) = 2 / (top - bottom);
        r.at(2, 2) = -2 / (farZ - nearZ);
        r.at(0, 3) = -(right + left) / (right - left);
        r.at(1, 3) = -(top + bottom) / (top - bottom);
        r.at(2, 3) = -(farZ + nearZ) / (farZ - nearZ);
        return r;
    }

    Mat4 operator*(const Mat4 &b) const
    {
        Mat4 r;
        for (int c = 0; c < 4; ++c) {
            for (int rr = 0; rr < 4; ++rr) {
                float s = 0;
                for (int k = 0; k < 4; ++k) {
                    s += at(rr, k) * b.at(k, c);
                }
                r.at(rr, c) = s;
            }
        }
        return r;
    }

    Vec4 operator*(const Vec4 &v) const
    {
        return {at(0, 0) * v.x + at(0, 1) * v.y + at(0, 2) * v.z + at(0, 3) * v.w,
                at(1, 0) * v.x + at(1, 1) * v.y + at(1, 2) * v.z + at(1, 3) * v.w,
                at(2, 0) * v.x + at(2, 1) * v.y + at(2, 2) * v.z + at(2, 3) * v.w,
                at(3, 0) * v.x + at(3, 1) * v.y + at(3, 2) * v.z + at(3, 3) * v.w};
    }

    Vec3 transformPoint(const Vec3 &p) const
    {
        const Vec4 r = *this * Vec4(p, 1.0f);
        return r.w != 0.0f && r.w != 1.0f ? r.xyz() / r.w : r.xyz();
    }

    Vec3 transformVector(const Vec3 &v) const { return (*this * Vec4(v, 0.0f)).xyz(); }

    Mat4 inverted() const;
};

inline Mat4 Mat4::inverted() const
{
    // Cofactor expansion; good enough for 4x4 view/projection matrices.
    const float *a = m;
    float inv[16];
    inv[0] = a[5] * a[10] * a[15] - a[5] * a[11] * a[14] - a[9] * a[6] * a[15] +
             a[9] * a[7] * a[14] + a[13] * a[6] * a[11] - a[13] * a[7] * a[10];
    inv[4] = -a[4] * a[10] * a[15] + a[4] * a[11] * a[14] + a[8] * a[6] * a[15] -
             a[8] * a[7] * a[14] - a[12] * a[6] * a[11] + a[12] * a[7] * a[10];
    inv[8] = a[4] * a[9] * a[15] - a[4] * a[11] * a[13] - a[8] * a[5] * a[15] +
             a[8] * a[7] * a[13] + a[12] * a[5] * a[11] - a[12] * a[7] * a[9];
    inv[12] = -a[4] * a[9] * a[14] + a[4] * a[10] * a[13] + a[8] * a[5] * a[14] -
              a[8] * a[6] * a[13] - a[12] * a[5] * a[10] + a[12] * a[6] * a[9];
    inv[1] = -a[1] * a[10] * a[15] + a[1] * a[11] * a[14] + a[9] * a[2] * a[15] -
             a[9] * a[3] * a[14] - a[13] * a[2] * a[11] + a[13] * a[3] * a[10];
    inv[5] = a[0] * a[10] * a[15] - a[0] * a[11] * a[14] - a[8] * a[2] * a[15] +
             a[8] * a[3] * a[14] + a[12] * a[2] * a[11] - a[12] * a[3] * a[10];
    inv[9] = -a[0] * a[9] * a[15] + a[0] * a[11] * a[13] + a[8] * a[1] * a[15] -
             a[8] * a[3] * a[13] - a[12] * a[1] * a[11] + a[12] * a[3] * a[9];
    inv[13] = a[0] * a[9] * a[14] - a[0] * a[10] * a[13] - a[8] * a[1] * a[14] +
              a[8] * a[2] * a[13] + a[12] * a[1] * a[10] - a[12] * a[2] * a[9];
    inv[2] = a[1] * a[6] * a[15] - a[1] * a[7] * a[14] - a[5] * a[2] * a[15] +
             a[5] * a[3] * a[14] + a[13] * a[2] * a[7] - a[13] * a[3] * a[6];
    inv[6] = -a[0] * a[6] * a[15] + a[0] * a[7] * a[14] + a[4] * a[2] * a[15] -
             a[4] * a[3] * a[14] - a[12] * a[2] * a[7] + a[12] * a[3] * a[6];
    inv[10] = a[0] * a[5] * a[15] - a[0] * a[7] * a[13] - a[4] * a[1] * a[15] +
              a[4] * a[3] * a[13] + a[12] * a[1] * a[7] - a[12] * a[3] * a[5];
    inv[14] = -a[0] * a[5] * a[14] + a[0] * a[6] * a[13] + a[4] * a[1] * a[14] -
              a[4] * a[2] * a[13] - a[12] * a[1] * a[6] + a[12] * a[2] * a[5];
    inv[3] = -a[1] * a[6] * a[11] + a[1] * a[7] * a[10] + a[5] * a[2] * a[11] -
             a[5] * a[3] * a[10] - a[9] * a[2] * a[7] + a[9] * a[3] * a[6];
    inv[7] = a[0] * a[6] * a[11] - a[0] * a[7] * a[10] - a[4] * a[2] * a[11] +
             a[4] * a[3] * a[10] + a[8] * a[2] * a[7] - a[8] * a[3] * a[6];
    inv[11] = -a[0] * a[5] * a[11] + a[0] * a[7] * a[9] + a[4] * a[1] * a[11] -
              a[4] * a[3] * a[9] - a[8] * a[1] * a[7] + a[8] * a[3] * a[5];
    inv[15] = a[0] * a[5] * a[10] - a[0] * a[6] * a[9] - a[4] * a[1] * a[10] +
              a[4] * a[2] * a[9] + a[8] * a[1] * a[6] - a[8] * a[2] * a[5];

    float det = a[0] * inv[0] + a[1] * inv[4] + a[2] * inv[8] + a[3] * inv[12];
    Mat4 r;
    if (det == 0.0f) {
        return r;
    }
    det = 1.0f / det;
    for (int i = 0; i < 16; ++i) {
        r.m[i] = inv[i] * det;
    }
    return r;
}

struct Ray {
    Vec3 origin;
    Vec3 direction; // normalized
};

} // namespace atherta
