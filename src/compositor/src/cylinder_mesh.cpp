#include "cylinder_mesh.hpp"
#include "deskwrap/common/math.hpp"
#include <cmath>

namespace deskwrap::compositor {

MeshData CylinderMeshGenerator::generate(const CylinderGeometry& geom) {
    MeshData mesh;

    uint32_t seg_u = geom.segments_u;
    uint32_t seg_v = geom.segments_v;

    float arc_rad = geom.arc_degrees * math::DEG_TO_RAD;
    float start_angle = -arc_rad * 0.5f;

    float half_h = geom.height * 0.5f;

    // Generate vertices
    for (uint32_t iv = 0; iv <= seg_v; ++iv) {
        float v_ratio = static_cast<float>(iv) / static_cast<float>(seg_v);
        float y = half_h - v_ratio * geom.height; // From top to bottom

        for (uint32_t iu = 0; iu <= seg_u; ++iu) {
            float u_ratio = static_cast<float>(iu) / static_cast<float>(seg_u);
            float angle = start_angle + u_ratio * arc_rad;

            // In Direct3D 11 view space:
            // +X is East (right), +Y is Up, +Z is North (forward)
            // Center is (0,0,0). Angle 0 is straight ahead along +Z.
            // Positive angle rotates towards +X.
            float sin_a = std::sin(angle);
            float cos_a = std::cos(angle);

            float x = geom.radius * sin_a;
            float z = geom.radius * cos_a;

            Vertex v{};
            v.pos[0] = x;
            v.pos[1] = y;
            v.pos[2] = z;

            v.uv[0] = u_ratio;
            v.uv[1] = v_ratio;

            // Inward-facing normal towards (0, y, 0)
            v.normal[0] = -sin_a;
            v.normal[1] = 0.0f;
            v.normal[2] = -cos_a;

            mesh.vertices.push_back(v);
        }
    }

    // Generate indices (winding for inward viewer)
    uint32_t stride = seg_u + 1;
    for (uint32_t iv = 0; iv < seg_v; ++iv) {
        for (uint32_t iu = 0; iu < seg_u; ++iu) {
            uint32_t top_left = iv * stride + iu;
            uint32_t top_right = top_left + 1;
            uint32_t bot_left = (iv + 1) * stride + iu;
            uint32_t bot_right = bot_left + 1;

            // First triangle (inward winding)
            mesh.indices.push_back(top_left);
            mesh.indices.push_back(bot_left);
            mesh.indices.push_back(top_right);

            // Second triangle
            mesh.indices.push_back(top_right);
            mesh.indices.push_back(bot_left);
            mesh.indices.push_back(bot_right);
        }
    }

    return mesh;
}

} // namespace deskwrap::compositor
