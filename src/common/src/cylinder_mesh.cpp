#include "deskwrap/common/cylinder_mesh.hpp"
#include <cmath>
#include <algorithm>

namespace deskwrap {

namespace {
constexpr float PI = 3.14159265358979323846f;
} // namespace

Matrix4x4 Matrix4x4::identity() {
    Matrix4x4 res{};
    for (int i = 0; i < 4; ++i) {
        res.m[i][i] = 1.0f;
    }
    return res;
}

Matrix4x4 Matrix4x4::perspective_lh(float fov_y_rad, float aspect, float near_z, float far_z) {
    Matrix4x4 res{};
    float tan_half_fov = std::tan(fov_y_rad * 0.5f);
    float y_scale = 1.0f / tan_half_fov;
    float x_scale = y_scale / aspect;

    res.m[0][0] = x_scale;
    res.m[1][1] = y_scale;
    res.m[2][2] = far_z / (far_z - near_z);
    res.m[2][3] = 1.0f;
    res.m[3][2] = (-near_z * far_z) / (far_z - near_z);
    return res;
}

Matrix4x4 Matrix4x4::rotation_yaw_pitch_roll(float yaw_rad, float pitch_rad, float roll_rad) {
    float cy = std::cos(yaw_rad);
    float sy = std::sin(yaw_rad);
    float cp = std::cos(pitch_rad);
    float sp = std::sin(pitch_rad);
    float cr = std::cos(roll_rad);
    float sr = std::sin(roll_rad);

    Matrix4x4 ry = identity();
    ry.m[0][0] = cy;
    ry.m[0][2] = -sy;
    ry.m[2][0] = sy;
    ry.m[2][2] = cy;

    Matrix4x4 rx = identity();
    rx.m[1][1] = cp;
    rx.m[1][2] = sp;
    rx.m[2][1] = -sp;
    rx.m[2][2] = cp;

    Matrix4x4 rz = identity();
    rz.m[0][0] = cr;
    rz.m[0][1] = sr;
    rz.m[1][0] = -sr;
    rz.m[1][1] = cr;

    // R = Ry * Rx * Rz
    return multiply(multiply(ry, rx), rz);
}

Matrix4x4 Matrix4x4::multiply(const Matrix4x4& a, const Matrix4x4& b) {
    Matrix4x4 c{};
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            float sum = 0.0f;
            for (int k = 0; k < 4; ++k) {
                sum += a.m[i][k] * b.m[k][j];
            }
            c.m[i][j] = sum;
        }
    }
    return c;
}

bool transform_to_ndc(const Matrix4x4& mat, const Vector3& in_pos, Vector3& out_ndc) {
    float x = in_pos.x * mat.m[0][0] + in_pos.y * mat.m[1][0] + in_pos.z * mat.m[2][0] + mat.m[3][0];
    float y = in_pos.x * mat.m[0][1] + in_pos.y * mat.m[1][1] + in_pos.z * mat.m[2][1] + mat.m[3][1];
    float z = in_pos.x * mat.m[0][2] + in_pos.y * mat.m[1][2] + in_pos.z * mat.m[2][2] + mat.m[3][2];
    float w = in_pos.x * mat.m[0][3] + in_pos.y * mat.m[1][3] + in_pos.z * mat.m[2][3] + mat.m[3][3];

    if (std::abs(w) < 1e-7f) {
        return false;
    }

    out_ndc.x = x / w;
    out_ndc.y = y / w;
    out_ndc.z = z / w;
    return true;
}

CylinderMesh generate_cylinder_mesh(
    float arc_degrees,
    float radius,
    float height,
    uint32_t segments_u,
    uint32_t segments_v
) {
    float clamped_arc = std::clamp(arc_degrees, 180.0f, 360.0f);
    float arc_rad = clamped_arc * (PI / 180.0f);

    CylinderMesh mesh;
    mesh.vertices.reserve((segments_u + 1) * (segments_v + 1));
    mesh.indices.reserve(segments_u * segments_v * 6);

    for (uint32_t i = 0; i <= segments_u; ++i) {
        float u = static_cast<float>(i) / static_cast<float>(segments_u);
        float theta = (u - 0.5f) * arc_rad;
        float sin_t = std::sin(theta);
        float cos_t = std::cos(theta);

        for (uint32_t j = 0; j <= segments_v; ++j) {
            float v = static_cast<float>(j) / static_cast<float>(segments_v);
            float y = (0.5f - v) * height;

            float x = radius * sin_t;
            float z = radius * cos_t;

            // Seamless wrap handling for 360 degrees:
            // Snap exact 3D coordinates from u=0 to eliminate any floating-point gap
            if (std::abs(clamped_arc - 360.0f) < 1e-3f && i == segments_u) {
                x = mesh.vertices[0 * (segments_v + 1) + j].x;
                y = mesh.vertices[0 * (segments_v + 1) + j].y;
                z = mesh.vertices[0 * (segments_v + 1) + j].z;
            }

            mesh.vertices.push_back({x, y, z, u, v});
        }
    }

    for (uint32_t i = 0; i < segments_u; ++i) {
        for (uint32_t j = 0; j < segments_v; ++j) {
            uint32_t tl = i * (segments_v + 1) + j;
            uint32_t bl = i * (segments_v + 1) + (j + 1);
            uint32_t tr = (i + 1) * (segments_v + 1) + j;
            uint32_t br = (i + 1) * (segments_v + 1) + (j + 1);

            // Triangle 1
            mesh.indices.push_back(tl);
            mesh.indices.push_back(tr);
            mesh.indices.push_back(br);

            // Triangle 2
            mesh.indices.push_back(tl);
            mesh.indices.push_back(br);
            mesh.indices.push_back(bl);
        }
    }

    return mesh;
}

} // namespace deskwrap
