#pragma once

#include <windows.h>
#include <functional>
#include <string>

namespace deskwrap::daemon {

struct VitureMonitorInfo {
    bool found = false;
    HMONITOR h_monitor = nullptr;
    RECT rect{};
    std::wstring device_name{};
};

using AttachCallback = std::function<void(int pid, const RECT& display_rect)>;
using DetachCallback = std::function<void()>;

class DeviceMonitor {
public:
    DeviceMonitor() = default;
    ~DeviceMonitor();

    bool initialize(HWND message_window);
    void shutdown();

    void set_callbacks(AttachCallback on_attach, DetachCallback on_detach) {
        on_attach_ = std::move(on_attach);
        on_detach_ = std::move(on_detach);
    }

    LRESULT handle_message(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

    bool is_viture_usb_present() const { return usb_present_; }
    bool is_viture_monitor_present() const { return monitor_present_; }

private:
    void evaluate_state(bool immediate = false);
    static bool parse_viture_vid_pid(const std::wstring& dev_path, int& out_pid);
    static BOOL CALLBACK monitor_enum_proc(HMONITOR h_mon, HDC hdc, LPRECT lprc, LPARAM lparam);

    HWND hwnd_ = nullptr;
    HDEVNOTIFY h_dev_notify_ = nullptr;

    bool usb_present_ = false;
    bool monitor_present_ = false;
    bool active_state_ = false;
    int current_pid_ = -1;
    RECT monitor_rect_{};

    AttachCallback on_attach_;
    DetachCallback on_detach_;

    static constexpr UINT_PTR TIMER_DEBOUNCE_ID = 1001;
    static constexpr UINT DEBOUNCE_DELAY_MS = 500;
};

} // namespace deskwrap::daemon
