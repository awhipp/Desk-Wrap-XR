#pragma once

#include "deskwrap/common/types.hpp"
#include <array>
#include <cmath>

namespace deskwrap::math {

constexpr float PI = 3.14159265358979323846f;
constexpr float DEG_TO_RAD = PI / 180.0f;
constexpr float RAD_TO_DEG = 180.0f / PI;

struct Vector3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;

    constexpr Vector3() = default;
    constexpr Vector3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}

    Vector3 operator+(const Vector3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vector3 operator-(const Vector3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vector3 operator*(float s) const { return {x * s, y * s, z * s}; }
    float dot(const Vector3& o) const { return x * o.x + y * o.y + z * o.z; }
    Vector3 cross(const Vector3& o) const {
        return {
            y * o.z - z * o.y,
            z * o.x - x * o.z,
            x * o.y - y * o.x
        };
    }
    float length() const { return std::sqrt(dot(*this)); }
    Vector3 normalized() const {
        float l = length();
        return l > 1e-6f ? (*this) * (1.0f / l) : Vector3{};
    }
};

struct Quaternion {
    float w = 1.0f;
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;

    constexpr Quaternion() = default;
    constexpr Quaternion(float w_, float x_, float y_, float z_) : w(w_), x(x_), y(y_), z(z_) {}

    Quaternion conjugate() const { return {w, -x, -y, -z}; }

    Quaternion operator*(const Quaternion& o) const {
        return {
            w * o.w - x * o.x - y * o.y - z * o.z,
            w * o.x + x * o.w + y * o.z - z * o.y,
            w * o.y - x * o.z + y * o.w + z * o.x,
            w * o.z + x * o.y - y * o.x + z * o.w
        };
    }

    Vector3 rotate(const Vector3& v) const {
        Quaternion qv(0.0f, v.x, v.y, v.z);
        Quaternion res = (*this) * qv * conjugate();
        return {res.x, res.y, res.z};
    }
};

/**
 * @brief 4x4 Row-Major matrix for Direct3D 11 / HLSL constant buffer upload.
 */
struct Matrix4x4 {
    std::array<float, 16> m{};

    static Matrix4x4 identity();
    static Matrix4x4 perspective_fov_lh(float fov_y_rad, float aspect, float near_z, float far_z);
    static Matrix4x4 look_at_lh(const Vector3& eye, const Vector3& at, const Vector3& up);
    static Matrix4x4 from_quaternion(const Quaternion& q);
    static Matrix4x4 transpose(const Matrix4x4& mat);

    Matrix4x4 operator*(const Matrix4x4& o) const;
};

/**
 * @brief Converts VITURE NWU pose quaternion to Direct3D 11 view space rotation.
 * Basis transformation:
 *   X_d3d = -Y_nwu
 *   Y_d3d = +Z_nwu
 *   Z_d3d = +X_nwu
 * Left-handed camera orientation: [qw, -qy, qz, qx]
 */
Quaternion nwu_to_d3d11_quaternion(const Quaternion& q_nwu);

/**
 * @brief Computes software yaw recentering offset rotation.
 */
Quaternion make_yaw_offset(float yaw_offset_deg);

} // namespace deskwrap::math
