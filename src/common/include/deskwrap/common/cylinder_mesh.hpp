#pragma once

#include <cstdint>
#include <vector>

namespace deskwrap {

/**
 * @brief Vertex format for the cylindrical projection mesh.
 */
struct CylinderVertex {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float u = 0.0f;
    float v = 0.0f;
};

/**
 * @brief Indexed triangle mesh for cylindrical projection.
 */
struct CylinderMesh {
    std::vector<CylinderVertex> vertices;
    std::vector<uint32_t> indices;
};

/**
 * @brief 3D vector for geometry and camera mathematics.
 */
struct Vector3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

/**
 * @brief 4x4 row-major matrix for transforms and perspective projection.
 */
struct Matrix4x4 {
    float m[4][4]{};

    static Matrix4x4 identity();
    static Matrix4x4 perspective_lh(float fov_y_rad, float aspect, float near_z, float far_z);
    static Matrix4x4 rotation_yaw_pitch_roll(float yaw_rad, float pitch_rad, float roll_rad);
    static Matrix4x4 multiply(const Matrix4x4& a, const Matrix4x4& b);
};

/**
 * @brief Transforms a 3D point by a 4x4 matrix and performs perspective division to NDC space.
 */
bool transform_to_ndc(const Matrix4x4& mat, const Vector3& in_pos, Vector3& out_ndc);

/**
 * @brief Generates an indexed cylinder mesh for curved desktop projection.
 *
 * The cylinder is centered around the viewer at the origin (0, 0, 0).
 * Angle theta is centered at forward (+Z in D3D11 LH):
 * u = 0 corresponds to the left edge of the desktop (negative X).
 * u = 0.5 corresponds to the center of the desktop (x = 0, z = +radius).
 * u = 1 corresponds to the right edge of the desktop (positive X).
 *
 * For a full 360° arc, the vertices at u = 0 and u = 1 are guaranteed to have
 * bit-for-bit identical 3D positions (0, y, -radius) to prevent any visible seam.
 *
 * @param arc_degrees Arc span in degrees (180.0° to 360.0°).
 * @param radius Radius of cylinder in meters (default 1.8m).
 * @param height Height of cylinder in meters (default 1.0m).
 * @param segments_u Mesh resolution around the arc (default 128).
 * @param segments_v Mesh resolution along the height (default 16).
 * @return CylinderMesh containing vertices and triangle indices.
 */
CylinderMesh generate_cylinder_mesh(
    float arc_degrees,
    float radius = 1.8f,
    float height = 1.0f,
    uint32_t segments_u = 128,
    uint32_t segments_v = 16
);

} // namespace deskwrap
