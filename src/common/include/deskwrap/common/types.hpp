#pragma once

#include <cstdint>
#include <string>

namespace deskwrap {

/**
 * @brief Resolution and refresh rate settings for virtual and physical displays.
 */
struct DisplayMode {
    uint32_t width = 5760;
    uint32_t height = 1080;
    uint32_t refresh_rate_hz = 60;
};

/**
 * @brief 3D cylindrical canvas projection parameters.
 */
struct CylinderGeometry {
    float radius = 1.8f;              // Distance from viewer center to cylinder surface (meters)
    float arc_degrees = 180.0f;       // Horizontal FOV span covered by desktop (degrees, up to 360)
    float height = 1.0f;              // Vertical height of cylindrical canvas (meters)
    uint32_t segments_u = 128;        // Mesh tessellation resolution along arc
    uint32_t segments_v = 16;         // Mesh tessellation resolution along height
};

/**
 * @brief Spatial 3DoF orientation packet.
 */
struct PosePacket {
    uint64_t timestamp_us = 0;
    float roll = 0.0f;                // Degrees
    float pitch = 0.0f;               // Degrees
    float yaw = 0.0f;                 // Degrees
    float qw = 1.0f;                  // Quaternion W (scalar)
    float qx = 0.0f;                  // Quaternion X
    float qy = 0.0f;                  // Quaternion Y
    float qz = 0.0f;                  // Quaternion Z
};

/**
 * @brief Runtime tracking and connection state of the XR headset.
 */
enum class GlassesConnectionState {
    Disconnected,
    PendingAttach,
    Connected,
    Streaming,
    Error
};

} // namespace deskwrap
