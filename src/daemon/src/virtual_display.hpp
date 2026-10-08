#pragma once

#include <windows.h>
#include <string>
#include "deskwrap/common/types.hpp"

namespace deskwrap::daemon {

class VirtualDisplayManager {
public:
    VirtualDisplayManager() = default;
    ~VirtualDisplayManager();

    bool enable_virtual_driver();
    bool disable_virtual_driver();

    bool apply_display_mode(const DisplayMode& mode);
    bool restore_display_mode();

    bool find_virtual_display_device(std::wstring& out_device_name);

private:
    std::wstring active_device_name_{};
    bool is_driver_enabled_ = false;
    bool is_mode_applied_ = false;
};

} // namespace deskwrap::daemon
