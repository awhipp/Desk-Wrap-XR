#include "device_monitor.hpp"
#include <dbt.h>
#include <initguid.h>
#include <usbiodef.h>
#include <algorithm>

namespace deskwrap::daemon {

DeviceMonitor::~DeviceMonitor() {
    shutdown();
}

bool DeviceMonitor::initialize(HWND message_window) {
    hwnd_ = message_window;

    DEV_BROADCAST_DEVICEINTERFACE_W filter{};
    filter.dbcc_size = sizeof(filter);
    filter.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
    filter.dbcc_classguid = GUID_DEVINTERFACE_USB_DEVICE;

    h_dev_notify_ = RegisterDeviceNotificationW(
        hwnd_,
        &filter,
        DEVICE_NOTIFY_WINDOW_HANDLE
    );

    // Initial check
    evaluate_state(true);
    return h_dev_notify_ != nullptr;
}

void DeviceMonitor::shutdown() {
    if (h_dev_notify_) {
        UnregisterDeviceNotification(h_dev_notify_);
        h_dev_notify_ = nullptr;
    }
    if (active_state_ && on_detach_) {
        on_detach_();
        active_state_ = false;
    }
}

LRESULT DeviceMonitor::handle_message(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    switch (msg) {
    case WM_DEVICECHANGE: {
        if (wparam == DBT_DEVICEARRIVAL || wparam == DBT_DEVICEREMOVECOMPLETE) {
            auto* p_hdr = reinterpret_cast<PDEV_BROADCAST_HDR>(lparam);
            if (p_hdr && p_hdr->dbch_devicetype == DBT_DEVTYP_DEVICEINTERFACE) {
                auto* p_dev = reinterpret_cast<PDEV_BROADCAST_DEVICEINTERFACE_W>(lparam);
                std::wstring dev_path(p_dev->dbcc_name);
                int pid = -1;
                if (parse_viture_vid_pid(dev_path, pid)) {
                    if (wparam == DBT_DEVICEARRIVAL) {
                        usb_present_ = true;
                        current_pid_ = pid;
                    } else {
                        usb_present_ = false;
                        current_pid_ = -1;
                    }
                    evaluate_state();
                }
            }
        }
        return TRUE;
    }

    case WM_DISPLAYCHANGE: {
        evaluate_state();
        return 0;
    }

    case WM_TIMER: {
        if (wparam == TIMER_DEBOUNCE_ID) {
            KillTimer(hwnd, TIMER_DEBOUNCE_ID);
            evaluate_state(true);
            return 0;
        }
        break;
    }
    }

    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

void DeviceMonitor::evaluate_state(bool immediate) {
    if (!immediate) {
        SetTimer(hwnd_, TIMER_DEBOUNCE_ID, DEBOUNCE_DELAY_MS, nullptr);
        return;
    }

    // Check monitor presence
    VitureMonitorInfo mon_info{};
    EnumDisplayMonitors(nullptr, nullptr, monitor_enum_proc, reinterpret_cast<LPARAM>(&mon_info));
    monitor_present_ = mon_info.found;
    if (monitor_present_) {
        monitor_rect_ = mon_info.rect;
    }

    bool should_be_active = usb_present_ && monitor_present_;

    if (should_be_active && !active_state_) {
        active_state_ = true;
        if (on_attach_) {
            on_attach_(current_pid_, monitor_rect_);
        }
    } else if (!should_be_active && active_state_) {
        active_state_ = false;
        if (on_detach_) {
            on_detach_();
        }
    }
}

bool DeviceMonitor::parse_viture_vid_pid(const std::wstring& dev_path, int& out_pid) {
    // Expected pattern: VID_35CA&PID_XXXX
    std::wstring lower_path = dev_path;
    std::transform(lower_path.begin(), lower_path.end(), lower_path.begin(), ::towlower);

    size_t vid_pos = lower_path.find(L"vid_35ca");
    if (vid_pos == std::wstring::npos) return false;

    size_t pid_pos = lower_path.find(L"pid_", vid_pos);
    if (pid_pos == std::wstring::npos || pid_pos + 8 > lower_path.size()) return false;

    std::wstring pid_str = lower_path.substr(pid_pos + 4, 4);
    try {
        out_pid = std::stoi(pid_str, nullptr, 16);
        return true;
    } catch (...) {
        return false;
    }
}

BOOL CALLBACK DeviceMonitor::monitor_enum_proc(HMONITOR h_mon, HDC, LPRECT, LPARAM lparam) {
    auto* info = reinterpret_cast<VitureMonitorInfo*>(lparam);
    MONITORINFOEXW mi{};
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(h_mon, &mi)) return TRUE;

    DISPLAY_DEVICEW disp_dev{};
    disp_dev.cbSize = sizeof(disp_dev);

    if (EnumDisplayDevicesW(mi.szDevice, 0, &disp_dev, EDD_GET_DEVICE_INTERFACE_NAME)) {
        DISPLAY_DEVICEW mon_dev{};
        mon_dev.cbSize = sizeof(mon_dev);
        if (EnumDisplayDevicesW(mi.szDevice, 0, &mon_dev, 0)) {
            if (wcsstr(mon_dev.DeviceID, L"VTR") != nullptr ||
                wcsstr(mon_dev.DeviceString, L"VITURE") != nullptr ||
                wcsstr(mon_dev.DeviceString, L"VTR") != nullptr) {
                info->found = true;
                info->h_monitor = h_mon;
                info->rect = mi.rcMonitor;
                info->device_name = mi.szDevice;
                return FALSE; // Stop enumeration
            }
        }
    }
    return TRUE;
}

} // namespace deskwrap::daemon
