#pragma once

#include <string>

#ifdef _WIN32
#include <windows.h>
#endif

namespace deskwrap::compositor {

/**
 * @brief Discovered monitor information for VITURE glasses.
 */
struct GlassesMonitorInfo {
    bool found = false;
    std::wstring adapter_name;
    std::string hardware_id;
    std::string edid_name;
#ifdef _WIN32
    RECT bounds{};
#endif
    int width = 0;
    int height = 0;
};

/**
 * @brief Checks if candidate device ID or friendly name matches target glasses hardware ID or EDID name.
 */
bool is_glasses_monitor_match(
    const std::string& candidate_device_id,
    const std::string& candidate_device_string,
    const std::string& target_hardware_id = "DISPLAY\\VIT0120",
    const std::string& target_edid_name = "VITURE Beast"
);

/**
 * @brief Enumerates attached display monitors to locate the VITURE glasses monitor.
 */
GlassesMonitorInfo find_glasses_monitor(
    const std::string& target_hardware_id = "DISPLAY\\VIT0120",
    const std::string& target_edid_name = "VITURE Beast"
);

std::string to_narrow_string(const std::wstring& wstr);

} // namespace deskwrap::compositor
