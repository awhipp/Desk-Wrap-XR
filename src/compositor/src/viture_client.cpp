#include "viture_client.hpp"
#include "viture_glasses_provider.h"
#include "viture_device.h"
#include "viture_protocol_public.h"
#include <iostream>

namespace deskwrap::compositor {

VitureClient* VitureClient::s_instance_ = nullptr;

VitureClient::~VitureClient() {
    shutdown();
}

bool VitureClient::initialize(int product_id) {
    shutdown();
    product_id_ = product_id;
    s_instance_ = this;

    provider_handle_ = xr_device_provider_create(product_id);
    if (!provider_handle_) {
        std::cerr << "[VitureClient] Failed to create device provider for PID: 0x"
                  << std::hex << product_id << std::endl;
        return false;
    }

    int res = xr_device_provider_initialize(provider_handle_, nullptr, nullptr);
    if (res != 0) {
        std::cerr << "[VitureClient] Failed to initialize provider (error: " << res << ")\n";
        xr_device_provider_destroy(provider_handle_);
        provider_handle_ = nullptr;
        return false;
    }

    xr_device_provider_register_state_callback(provider_handle_, on_state_callback);

    // Enter Bypass Mode (0) to obtain raw 3DoF tracking for spatial cylinder
    xr_device_provider_native_set_mode(provider_handle_, 0);

    // Set 1920x1200 @ 60Hz mode
    xr_device_provider_set_display_mode(provider_handle_, VITURE_DISPLAY_MODE_1920_1200_60HZ);

    // Register IMU pose callback
    xr_device_provider_register_imu_pose_callback(provider_handle_, on_pose_callback);

    // Determine highest supported IMU frequency
    uint8_t freq = VITURE_IMU_FREQUENCY_HIGH; // 500Hz
    while (freq > VITURE_IMU_FREQUENCY_LOW &&
           !xr_device_provider_is_product_support_imu_frequency(product_id, VITURE_IMU_MODE_POSE, freq)) {
        freq--;
    }

    res = xr_device_provider_open_imu(provider_handle_, VITURE_IMU_MODE_POSE, freq);
    if (res != 0) {
        std::cerr << "[VitureClient] Failed to open IMU stream (error: " << res << ")\n";
        xr_device_provider_shutdown(provider_handle_);
        xr_device_provider_destroy(provider_handle_);
        provider_handle_ = nullptr;
        return false;
    }

    xr_device_provider_start(provider_handle_);
    is_streaming_ = true;
    std::cout << "[VitureClient] IMU pose stream started at frequency index: "
              << static_cast<int>(freq) << std::endl;
    return true;
}

void VitureClient::shutdown() {
    if (provider_handle_) {
        if (is_streaming_) {
            xr_device_provider_close_imu(provider_handle_, VITURE_IMU_MODE_POSE);
            xr_device_provider_stop(provider_handle_);
            is_streaming_ = false;
        }
        xr_device_provider_shutdown(provider_handle_);
        xr_device_provider_destroy(provider_handle_);
        provider_handle_ = nullptr;
    }
    if (s_instance_ == this) {
        s_instance_ = nullptr;
    }
}

void VitureClient::recenter() {
    std::lock_guard<std::mutex> lock(pose_mutex_);
    yaw_offset_deg_ = latest_pose_.yaw;
    std::cout << "[VitureClient] Recentered software yaw to offset: "
              << yaw_offset_deg_ << " deg\n";
}

void VitureClient::set_yaw_offset(float offset_deg) {
    std::lock_guard<std::mutex> lock(pose_mutex_);
    yaw_offset_deg_ = offset_deg;
}

PosePacket VitureClient::get_latest_pose() const {
    std::lock_guard<std::mutex> lock(pose_mutex_);
    return latest_pose_;
}

math::Quaternion VitureClient::get_camera_orientation() const {
    std::lock_guard<std::mutex> lock(pose_mutex_);
    // Convert NWU quaternion to D3D11 Left-Handed
    math::Quaternion raw_q(latest_pose_.qw, latest_pose_.qx, latest_pose_.qy, latest_pose_.qz);
    math::Quaternion d3d_q = math::nwu_to_d3d11_quaternion(raw_q);

    // Apply software yaw offset around Up (+Y) to cancel offset angle
    math::Quaternion yaw_offset = math::make_yaw_offset(-yaw_offset_deg_);
    return yaw_offset * d3d_q;
}

void VitureClient::on_pose_callback(float* data, uint64_t timestamp) {
    if (!s_instance_ || !data) return;

    std::lock_guard<std::mutex> lock(s_instance_->pose_mutex_);
    s_instance_->latest_pose_.timestamp_us = timestamp;
    s_instance_->latest_pose_.roll = data[0];
    s_instance_->latest_pose_.pitch = data[1];
    s_instance_->latest_pose_.yaw = data[2];
    s_instance_->latest_pose_.qw = data[3];
    s_instance_->latest_pose_.qx = data[4];
    s_instance_->latest_pose_.qy = data[5];
    s_instance_->latest_pose_.qz = data[6];
}

void VitureClient::on_state_callback(int glass_state_id, int glass_value) {
    std::cout << "[VitureClient] Glass state notification - ID: " << glass_state_id
              << " Value: " << glass_value << std::endl;
}

} // namespace deskwrap::compositor
