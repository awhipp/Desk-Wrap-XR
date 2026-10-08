#include "mouse_hook.hpp"

namespace deskwrap::daemon {

HHOOK MouseSeamHook::hook_handle_ = nullptr;
int32_t MouseSeamHook::origin_x_ = 0;
int32_t MouseSeamHook::origin_y_ = 0;
int32_t MouseSeamHook::width_ = 5760;
int32_t MouseSeamHook::height_ = 1080;
bool MouseSeamHook::is_warping_ = false;

MouseSeamHook::~MouseSeamHook() {
    uninstall();
}

bool MouseSeamHook::install(int32_t origin_x, int32_t origin_y, int32_t width, int32_t height) {
    uninstall();
    origin_x_ = origin_x;
    origin_y_ = origin_y;
    width_ = width;
    height_ = height;
    is_warping_ = false;

    hook_handle_ = SetWindowsHookExW(
        WH_MOUSE_LL,
        LowLevelMouseProc,
        GetModuleHandleW(nullptr),
        0
    );

    return hook_handle_ != nullptr;
}

void MouseSeamHook::uninstall() {
    if (hook_handle_) {
        UnhookWindowsHookEx(hook_handle_);
        hook_handle_ = nullptr;
    }
}

LRESULT CALLBACK MouseSeamHook::LowLevelMouseProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION && (wParam == WM_MOUSEMOVE)) {
        if (is_warping_) {
            is_warping_ = false;
            return CallNextHookEx(hook_handle_, nCode, wParam, lParam);
        }

        auto* p_mouse = reinterpret_cast<MSLLHOOKSTRUCT*>(lParam);
        POINT pt = p_mouse->pt;

        // Check if cursor is vertically inside the cylinder desktop bounds
        if (pt.y >= origin_y_ && pt.y < origin_y_ + height_) {
            int32_t min_x = origin_x_;
            int32_t max_x = origin_x_ + width_ - 1;

            if (pt.x <= min_x) {
                // Crossing left border -> wrap to right edge
                is_warping_ = true;
                SetCursorPos(max_x - 2, pt.y);
                return 1; // Handled
            } else if (pt.x >= max_x) {
                // Crossing right border -> wrap to left edge
                is_warping_ = true;
                SetCursorPos(min_x + 2, pt.y);
                return 1; // Handled
            }
        }
    }

    return CallNextHookEx(hook_handle_, nCode, wParam, lParam);
}

} // namespace deskwrap::daemon
