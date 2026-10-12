#include <iostream>
#include <string>
#include <filesystem>
#include <atomic>
#include <chrono>
#include <memory>
#include "deskwrap/common/config.hpp"
#include "deskwrap/common/ipc_channel.hpp"
#include "cylinder_renderer.hpp"
#include "monitor_finder.hpp"

#ifdef _WIN32
#include <windows.h>
#endif

// Include VITURE SDK header to confirm SDK link integration
#include "viture_glasses_provider.h"

namespace {

deskwrap::compositor::CylinderRenderer* g_renderer = nullptr;
std::atomic<bool> g_connected{false};

#ifdef _WIN32
LRESULT CALLBACK window_proc(HWND hwnd, UINT msg, WPARAM w_param, LPARAM l_param) {
    switch (msg) {
    case WM_SIZE: {
        uint32_t w = LOWORD(l_param);
        uint32_t h = HIWORD(l_param);
        if (g_renderer && w > 0 && h > 0) {
            g_renderer->resize(w, h);
        }
        return 0;
    }
    case WM_KEYDOWN: {
        if (!g_renderer) return 0;
        switch (w_param) {
        case VK_LEFT:
            g_renderer->adjust_camera_yaw(5.0f);
            break;
        case VK_RIGHT:
            g_renderer->adjust_camera_yaw(-5.0f);
            break;
        case VK_UP:
            g_renderer->adjust_camera_pitch(5.0f);
            break;
        case VK_DOWN:
            g_renderer->adjust_camera_pitch(-5.0f);
            break;
        case 'R':
            g_renderer->reset_camera();
            break;
        case VK_OEM_PLUS:
        case VK_ADD:
            g_renderer->update_geometry(g_renderer->get_arc_degrees() + 10.0f, 1.8f, 1.0f);
            break;
        case VK_OEM_MINUS:
        case VK_SUBTRACT:
            g_renderer->update_geometry(g_renderer->get_arc_degrees() - 10.0f, 1.8f, 1.0f);
            break;
        case VK_ESCAPE:
            PostQuitMessage(0);
            break;
        default:
            break;
        }
        return 0;
    }
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(hwnd, msg, w_param, l_param);
    }
}
#endif

} // namespace

int main(int argc, char* argv[]) {
    std::filesystem::path config_path;
    bool validate_only = false;
    bool preview_mode = false;
    std::wstring pipe_name = deskwrap::ipc::DEFAULT_PIPE_NAME;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--config" && i + 1 < argc) {
            config_path = argv[++i];
        } else if (arg == "--validate-config") {
            validate_only = true;
        } else if (arg == "--pipe" && i + 1 < argc) {
            std::string p = argv[++i];
            pipe_name = std::wstring(p.begin(), p.end());
        } else if (arg == "--preview" || arg == "--windowed") {
            preview_mode = true;
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "DeskWrap XR Compositor\n"
                      << "Usage: deskwrap-compositor [options]\n"
                      << "Options:\n"
                      << "  --config <path>      Specify custom path to config.json\n"
                      << "  --validate-config    Validate config loading and exit\n"
                      << "  --pipe <name>        Named pipe name for IPC\n"
                      << "  --preview, --windowed Run in windowed preview mode on primary display\n"
                      << "  --help, -h           Show this help message\n";
            return 0;
        }
    }

    std::cout << "[Compositor] Initializing DeskWrap XR Compositor...\n";

    deskwrap::ConfigLoadResult config_res = deskwrap::load_config(config_path);
    const auto& config = config_res.config;
    deskwrap::print_config(config, std::cout);

    if (validate_only) {
        std::cout << "[Compositor] Configuration validation completed.\n";
        return (config_res.status == deskwrap::ConfigLoadStatus::MalformedJson) ? 2 : 0;
    }

    std::cout << "[Compositor] Compositor configured with cylinder arc: " << config.arc_degrees << "°.\n";
    std::cout << "[Compositor] Target Monitor HW ID: " << config.monitor_hardware_id << "\n";
    std::cout << "[Compositor] Target Monitor EDID Name: " << config.monitor_edid_name << "\n";
    std::cout << "[Compositor] Pose Prediction: " << (config.pose_prediction_enabled ? "ON" : "OFF") << "\n";

#ifdef _WIN32
    // Search for the glasses monitor
    auto mon_info = deskwrap::compositor::find_glasses_monitor(
        config.monitor_hardware_id,
        config.monitor_edid_name
    );

    if (!mon_info.found) {
        if (!preview_mode) {
            std::cerr << "[Compositor] Error: Glasses monitor is absent: hardware ID '"
                      << config.monitor_hardware_id << "' (EDID: '"
                      << config.monitor_edid_name << "') not detected. Exiting.\n";
            return 1;
        } else {
            std::cout << "[Compositor] Glasses monitor absent. Running in preview/windowed mode on primary display as requested.\n";
        }
    }

    // Register Window Class
    const wchar_t* CLASS_NAME = L"DeskWrapCompositorWindowClass";
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = window_proc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = CLASS_NAME;

    RegisterClassExW(&wc);

    int win_x = 0;
    int win_y = 0;
    int win_w = 1280;
    int win_h = 720;
    DWORD window_style = 0;
    DWORD window_ex_style = 0;
    const wchar_t* window_title = L"DeskWrap XR Compositor - Glasses Output";

    if (mon_info.found) {
        // Open borderless fullscreen window on the glasses monitor without manual placement
        win_x = mon_info.bounds.left;
        win_y = mon_info.bounds.top;
        win_w = mon_info.width;
        win_h = mon_info.height;
        window_style = WS_POPUP | WS_VISIBLE;
        window_ex_style = WS_EX_APPWINDOW;

        std::cout << "[Compositor] Found glasses monitor '" << mon_info.edid_name
                  << "' (" << mon_info.hardware_id << ") on "
                  << deskwrap::compositor::to_narrow_string(mon_info.adapter_name)
                  << " at (" << win_x << ", " << win_y << ") with resolution "
                  << win_w << "x" << win_h << ".\n";
    } else {
        // Preview windowed mode
        win_w = 1280;
        win_h = 720;
        RECT wr = {0, 0, static_cast<LONG>(win_w), static_cast<LONG>(win_h)};
        AdjustWindowRect(&wr, WS_OVERLAPPEDWINDOW, FALSE);
        win_x = CW_USEDEFAULT;
        win_y = CW_USEDEFAULT;
        win_w = wr.right - wr.left;
        win_h = wr.bottom - wr.top;
        window_style = WS_OVERLAPPEDWINDOW | WS_VISIBLE;
        window_title = L"DeskWrap XR Compositor - Preview Window";
    }

    HWND hwnd = CreateWindowExW(
        window_ex_style,
        CLASS_NAME,
        window_title,
        window_style,
        win_x,
        win_y,
        win_w,
        win_h,
        nullptr,
        nullptr,
        GetModuleHandleW(nullptr),
        nullptr
    );

    if (!hwnd) {
        std::cerr << "[Compositor] Failed to create compositor window.\n";
        return 1;
    }

    if (mon_info.found) {
        SetWindowPos(hwnd, HWND_TOP, win_x, win_y, win_w, win_h, SWP_FRAMECHANGED | SWP_SHOWWINDOW);
        std::cout << "[Compositor] Opened fullscreen window on glasses monitor without manual placement.\n";
    }

    deskwrap::compositor::CylinderRenderer renderer;
    g_renderer = &renderer;

    uint32_t render_w = (mon_info.found) ? static_cast<uint32_t>(mon_info.width) : 1280;
    uint32_t render_h = (mon_info.found) ? static_cast<uint32_t>(mon_info.height) : 720;

    if (!renderer.initialize(hwnd, render_w, render_h, config)) {
        std::cerr << "[Compositor] Failed to initialize Direct3D 11 cylinder renderer.\n";
        return 1;
    }

    // Initialize IPC Client
    deskwrap::ipc::IpcClient ipc_client;
    ipc_client.set_on_message_callback([&](const deskwrap::ipc::MessageHeader& header, const std::vector<uint8_t>& payload) {
        if (header.type == deskwrap::ipc::MessageType::SharedTextureHandle) {
            if (payload.size() >= sizeof(deskwrap::ipc::SharedTexturePayload)) {
                deskwrap::ipc::SharedTexturePayload tex_payload{};
                std::memcpy(&tex_payload, payload.data(), sizeof(tex_payload));
                std::cout << "[Compositor] Received shared desktop texture (" << tex_payload.width
                          << "x" << tex_payload.height
                          << ", KeyedMutex: " << (tex_payload.has_keyed_mutex ? "YES" : "NO") << ").\n";
                renderer.import_shared_texture(tex_payload);
            }
        } else if (header.type == deskwrap::ipc::MessageType::HelloAck) {
            std::cout << "[Compositor] Daemon handshake confirmed.\n";
        }
    });

    ipc_client.set_on_disconnect_callback([&]() {
        std::cout << "[Compositor] Connection to daemon lost. Releasing shared texture and entering reconnect loop...\n";
        renderer.release_shared_texture();
        g_connected = false;
    });

    std::cout << "[Compositor] Window created and cylinder renderer active. Connecting to daemon...\n";

    auto last_reconnect_attempt = std::chrono::steady_clock::now() - std::chrono::seconds(1);
    bool running = true;
    MSG msg{};

    while (running) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                running = false;
                break;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        if (!running) break;

        // Auto-reconnect management
        if (!g_connected.load()) {
            auto now = std::chrono::steady_clock::now();
            if (now - last_reconnect_attempt >= std::chrono::milliseconds(300)) {
                last_reconnect_attempt = now;
                if (ipc_client.connect(pipe_name, 100)) {
                    g_connected = true;
                    std::cout << "[Compositor] Connected to daemon IPC pipe. Sending Hello...\n";
                    ipc_client.send_hello(GetCurrentProcessId());
                }
            }
        }

        renderer.render_frame();
    }

    std::cout << "[Compositor] Shutting down compositor...\n";
    ipc_client.disconnect();
    renderer.shutdown();
    g_renderer = nullptr;
#endif

    std::cout << "[Compositor] Compositor terminated cleanly.\n";
    return 0;
}
