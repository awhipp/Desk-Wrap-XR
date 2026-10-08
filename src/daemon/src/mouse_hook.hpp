#pragma once

#include <windows.h>
#include <cstdint>

namespace deskwrap::daemon {

class MouseSeamHook {
public:
    MouseSeamHook() = default;
    ~MouseSeamHook();

    bool install(int32_t origin_x, int32_t origin_y, int32_t width, int32_t height);
    void uninstall();

    static LRESULT CALLBACK LowLevelMouseProc(int nCode, WPARAM wParam, LPARAM lParam);

private:
    static HHOOK hook_handle_;
    static int32_t origin_x_;
    static int32_t origin_y_;
    static int32_t width_;
    static int32_t height_;
    static bool is_warping_;
};

} // namespace deskwrap::daemon
