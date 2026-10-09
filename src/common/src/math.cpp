#include "deskwrap/common/math.hpp"

namespace deskwrap::math {

Matrix4x4 Matrix4x4::identity() {
    Matrix4x4 res{};
    res.m[0] = 1.0f;
    res.m[5] = 1.0f;
    res.m[10] = 1.0f;
    res.m[15] = 1.0f;
    return res;
}

Matrix4x4 Matrix4x4::perspective_fov_lh(float fov_y_rad, float aspect, float near_z, float far_z) {
    Matrix4x4 res{};
    float h = 1.0f / std::tan(fov_y_rad * 0.5f);
    float w = h / aspect;
    float d = far_z / (far_z - near_z);

    res.m[0] = w;
    res.m[5] = h;
    res.m[10] = d;
    res.m[11] = 1.0f;
    res.m[14] = -near_z * d;
    return res;
}

Matrix4x4 Matrix4x4::look_at_lh(const Vector3& eye, const Vector3& at, const Vector3& up) {
    Vector3 z_axis = (at - eye).normalized();
    Vector3 x_axis = up.cross(z_axis).normalized();
    Vector3 y_axis = z_axis.cross(x_axis);

    Matrix4x4 res = identity();
    res.m[0] = x_axis.x;
    res.m[1] = y_axis.x;
    res.m[2] = z_axis.x;

    res.m[4] = x_axis.y;
    res.m[5] = y_axis.y;
    res.m[6] = z_axis.y;

    res.m[8] = x_axis.z;
    res.m[9] = y_axis.z;
    res.m[10] = z_axis.z;

    res.m[12] = -x_axis.dot(eye);
    res.m[13] = -y_axis.dot(eye);
    res.m[14] = -z_axis.dot(eye);
    return res;
}

Matrix4x4 Matrix4x4::from_quaternion(const Quaternion& q) {
    Matrix4x4 res = identity();
    float xx = q.x * q.x;
    float yy = q.y * q.y;
    float zz = q.z * q.z;
    float xy = q.x * q.y;
    float xz = q.x * q.z;
    float yz = q.y * q.z;
    float wx = q.w * q.x;
    float wy = q.w * q.y;
    float wz = q.w * q.z;

    res.m[0] = 1.0f - 2.0f * (yy + zz);
    res.m[1] = 2.0f * (xy + wz);
    res.m[2] = 2.0f * (xz - wy);

    res.m[4] = 2.0f * (xy - wz);
    res.m[5] = 1.0f - 2.0f * (xx + zz);
    res.m[6] = 2.0f * (yz + wx);

    res.m[8] = 2.0f * (xz + wy);
    res.m[9] = 2.0f * (yz - wx);
    res.m[10] = 1.0f - 2.0f * (xx + yy);
    return res;
}

Matrix4x4 Matrix4x4::transpose(const Matrix4x4& mat) {
    Matrix4x4 res{};
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            res.m[r * 4 + c] = mat.m[c * 4 + r];
        }
    }
    return res;
}

Matrix4x4 Matrix4x4::operator*(const Matrix4x4& o) const {
    Matrix4x4 res{};
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            float sum = 0.0f;
            for (int k = 0; k < 4; ++k) {
                sum += m[r * 4 + k] * o.m[k * 4 + c];
            }
            res.m[r * 4 + c] = sum;
        }
    }
    return res;
}

Quaternion nwu_to_d3d11_quaternion(const Quaternion& q_nwu) {
    // Direct3D 11 left-handed view space conversion
    // Basis mapping: X_d3d = -Y_nwu, Y_d3d = +Z_nwu, Z_d3d = +X_nwu
    return Quaternion(q_nwu.w, -q_nwu.y, q_nwu.z, q_nwu.x);
}

Quaternion make_yaw_offset(float yaw_offset_deg) {
    float half_rad = (yaw_offset_deg * DEG_TO_RAD) * 0.5f;
    // Rotation around Up (+Y in D3D11)
    return Quaternion(std::cos(half_rad), 0.0f, std::sin(half_rad), 0.0f);
}

Vector3 compute_angular_velocity(const Quaternion& q_prev, const Quaternion& q_curr, float dt_seconds) {
    if (dt_seconds <= 1e-7f) {
        return Vector3{};
    }

    // Relative rotation from prev to curr: q_diff = q_curr * q_prev^*
    Quaternion q_diff = q_curr * q_prev.conjugate();
    if (q_diff.w < 0.0f) {
        // Enforce shortest geodesic path
        q_diff.w = -q_diff.w;
        q_diff.x = -q_diff.x;
        q_diff.y = -q_diff.y;
        q_diff.z = -q_diff.z;
    }

    float vec_len = std::sqrt(q_diff.x * q_diff.x + q_diff.y * q_diff.y + q_diff.z * q_diff.z);
    if (vec_len < 1e-7f) {
        return Vector3{};
    }

    float angle = 2.0f * std::atan2(vec_len, q_diff.w);
    float scale = angle / (vec_len * dt_seconds);
    return Vector3(q_diff.x * scale, q_diff.y * scale, q_diff.z * scale);
}

Quaternion predict_orientation(const Quaternion& current, const Vector3& angular_velocity, float lookahead_seconds) {
    float omega_mag = angular_velocity.length();
    if (omega_mag < 1e-7f || lookahead_seconds <= 0.0f) {
        return current;
    }

    float angle = omega_mag * lookahead_seconds;
    float half_angle = angle * 0.5f;
    float sin_half = std::sin(half_angle);
    float inv_mag = 1.0f / omega_mag;

    Quaternion delta_q(
        std::cos(half_angle),
        angular_velocity.x * inv_mag * sin_half,
        angular_velocity.y * inv_mag * sin_half,
        angular_velocity.z * inv_mag * sin_half
    );

    return normalize_quaternion(delta_q * current);
}

float quaternion_angular_distance_deg(const Quaternion& q1, const Quaternion& q2) {
    float dot = std::abs(q1.w * q2.w + q1.x * q2.x + q1.y * q2.y + q1.z * q2.z);
    if (dot > 1.0f) dot = 1.0f;
    float angle_rad = 2.0f * std::acos(dot);
    return angle_rad * RAD_TO_DEG;
}

} // namespace deskwrap::math
