#pragma once

#include <vector>
#include <cstdint>
#include "deskwrap/common/types.hpp"

namespace deskwrap::compositor {

struct Vertex {
    float pos[3];
    float uv[2];
    float normal[3];
};

struct MeshData {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
};

class CylinderMeshGenerator {
public:
    static MeshData generate(const CylinderGeometry& geom);
};

} // namespace deskwrap::compositor
