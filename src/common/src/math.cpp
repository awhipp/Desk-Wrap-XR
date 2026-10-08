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

} // namespace deskwrap::math
