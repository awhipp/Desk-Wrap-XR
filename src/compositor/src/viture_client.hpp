#pragma once

#include <cstdint>
#include <mutex>
#include "deskwrap/common/types.hpp"
#include "deskwrap/common/math.hpp"

// Forward declaration of VITURE C API handle
typedef void* XRDeviceProviderHandle;

namespace deskwrap::compositor {

class VitureClient {
public:
    VitureClient() = default;
    ~VitureClient();

    bool initialize(int product_id);
    void shutdown();

    void recenter();
    void set_yaw_offset(float offset_deg);

    PosePacket get_latest_pose() const;
    math::Quaternion get_camera_orientation() const;
    bool is_streaming() const { return is_streaming_; }

private:
    static void on_pose_callback(float* data, uint64_t timestamp);
    static void on_state_callback(int glass_state_id, int glass_value);

    XRDeviceProviderHandle provider_handle_ = nullptr;
    int product_id_ = 0;
    bool is_streaming_ = false;

    mutable std::mutex pose_mutex_;
    PosePacket latest_pose_{};
    float yaw_offset_deg_ = 0.0f;

    static VitureClient* s_instance_;
};

} // namespace deskwrap::compositor
