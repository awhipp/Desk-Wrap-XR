#pragma once

#include <string>
#include <filesystem>
#include <iostream>
#include "deskwrap/common/types.hpp"

namespace deskwrap {

/**
 * @brief Application configuration for Desk Wrap XR daemon and compositor.
 */
struct DeskWrapConfig {
    // Spatial cylinder projection geometry
    float arc_degrees = 180.0f;          // Default 180° arc (bounds 180° - 360°)
    float min_arc_degrees = 180.0f;      // Minimum allowed arc degrees
    float max_arc_degrees = 360.0f;      // Maximum allowed arc degrees
    float radius = 1.8f;                 // Distance from viewer center to cylinder surface (meters)
    float height = 1.0f;                 // Vertical height of cylindrical canvas (meters)

    // Head tracking & latency mitigation
    bool pose_prediction_enabled = true; // Constant-velocity angular pose prediction

    // Hardware IDs and Display settings
    std::string monitor_hardware_id = "DISPLAY\\VIT0120"; // Hardware ID measured in #23
    std::string monitor_edid_name = "VITURE Beast";       // EDID monitor name from #23
    uint32_t viture_usb_vid = 0x35CA;                     // VITURE USB Vendor ID
    uint32_t viture_usb_pid = 0x1201;                     // VITURE Beast Product ID

    // Inter-process communication
    std::string pipe_name = "\\\\.\\pipe\\deskwrap_ipc";

    // Virtual display configuration
    DisplayMode virtual_display_mode{5760, 1080, 60};
};

/**
 * @brief Status returned from configuration loading.
 */
enum class ConfigLoadStatus {
    Success,
    FileNotFound,
    MalformedJson
};

/**
 * @brief Detailed result of loading a configuration file.
 */
struct ConfigLoadResult {
    DeskWrapConfig config;
    ConfigLoadStatus status = ConfigLoadStatus::Success;
    std::string message;
};

/**
 * @brief Returns the default path for the configuration file (%APPDATA%\DeskWrapXR\config.json).
 */
std::filesystem::path get_default_config_path();

/**
 * @brief Loads configuration from file.
 *
 * If filepath is empty, the default path (%APPDATA%\DeskWrapXR\config.json) is used.
 * If the file is missing, logs that config is missing, sets status to FileNotFound,
 * and returns default DeskWrapConfig (including 180° arc).
 * If the file contains malformed JSON, logs an error to std::cerr, sets status to MalformedJson,
 * and returns default DeskWrapConfig.
 * If the file is valid JSON, updates fields and returns DeskWrapConfig with status Success.
 *
 * @param filepath Path to config file, or empty for default.
 * @return ConfigLoadResult containing loaded config, status, and log message.
 */
ConfigLoadResult load_config(const std::filesystem::path& filepath = {});

/**
 * @brief Serializes a DeskWrapConfig to a formatted JSON string.
 */
std::string serialize_config_json(const DeskWrapConfig& config);

/**
 * @brief Saves a DeskWrapConfig to disk as JSON. Creates directories if needed.
 */
bool save_config(const DeskWrapConfig& config, const std::filesystem::path& filepath = {});

/**
 * @brief Prints human-readable configuration summary to an output stream.
 */
void print_config(const DeskWrapConfig& config, std::ostream& os = std::cout);

} // namespace deskwrap
