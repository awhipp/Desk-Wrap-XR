#include <windows.h>
#include <iostream>
#include "virtual_display.hpp"
#include "dxgi_capture.hpp"
#include "mouse_hook.hpp"
#include "device_monitor.hpp"
#include "deskwrap/common/ipc_channel.hpp"

namespace {
deskwrap::daemon::DeviceMonitor g_monitor;
deskwrap::daemon::VirtualDisplayManager g_display_mgr;
deskwrap::daemon::DxgiDesktopCapture g_capture;
deskwrap::daemon::MouseSeamHook g_mouse_hook;
deskwrap::ipc::IpcServer g_ipc;

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    if (msg == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }
    return g_monitor.handle_message(hwnd, msg, wparam, lparam);
}
} // namespace

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
    const wchar_t CLASS_NAME[] = L"DeskWrapDaemonMessageWindow";

    WNDCLASSW wc{};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;

    if (!RegisterClassW(&wc)) {
        return 1;
    }

    HWND hwnd = CreateWindowExW(
        0,
        CLASS_NAME,
        L"DeskWrap Daemon Monitor",
        0,
        0, 0, 0, 0,
        HWND_MESSAGE,
        nullptr,
        hInstance,
        nullptr
    );

    if (!hwnd) {
        return 1;
    }

    g_monitor.set_callbacks(
        [](int pid, const RECT& display_rect) {
            std::wcout << L"[Daemon] Glasses attached! PID: 0x" << std::hex << pid
                       << L" Display: (" << display_rect.left << L"," << display_rect.top << L")\n";

            deskwrap::DisplayMode mode{5760, 1080, 60};
            g_display_mgr.enable_virtual_driver();
            g_display_mgr.apply_display_mode(mode);

            std::wstring virtual_dev;
            if (g_display_mgr.find_virtual_display_device(virtual_dev)) {
                g_capture.initialize(virtual_dev, mode);
                g_mouse_hook.install(0, 0, mode.width, mode.height);

                g_ipc.start();
                auto payload = g_capture.get_shared_texture_payload();
                g_ipc.send_shared_texture(payload);
            }
        },
        []() {
            std::wcout << L"[Daemon] Glasses detached. Tearing down...\n";
            g_ipc.stop();
            g_mouse_hook.uninstall();
            g_capture.shutdown();
            g_display_mgr.restore_display_mode();
            g_display_mgr.disable_virtual_driver();
        }
    );

    g_monitor.initialize(hwnd);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    g_monitor.shutdown();
    return 0;
}
