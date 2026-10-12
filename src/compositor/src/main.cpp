#include <iostream>
#include <string>
#include <filesystem>
#include <atomic>
#include <chrono>
#include <memory>
#include "deskwrap/common/config.hpp"
#include "deskwrap/common/ipc_channel.hpp"
#include "flat_renderer.hpp"

#ifdef _WIN32
#include <windows.h>
#endif

// Include VITURE SDK header to confirm SDK link integration
#include "viture_glasses_provider.h"

namespace {

deskwrap::compositor::FlatRenderer* g_renderer = nullptr;
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
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "DeskWrap XR Compositor\n"
                      << "Usage: deskwrap-compositor [options]\n"
                      << "Options:\n"
                      << "  --config <path>      Specify custom path to config.json\n"
                      << "  --validate-config    Validate config loading and exit\n"
                      << "  --pipe <name>        Named pipe name for IPC\n"
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
    std::cout << "[Compositor] Pose Prediction: " << (config.pose_prediction_enabled ? "ON" : "OFF") << "\n";

#ifdef _WIN32
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

    uint32_t initial_w = 1280;
    uint32_t initial_h = 720;

    RECT wr = {0, 0, static_cast<LONG>(initial_w), static_cast<LONG>(initial_h)};
    AdjustWindowRect(&wr, WS_OVERLAPPEDWINDOW, FALSE);

    HWND hwnd = CreateWindowExW(
        0,
        CLASS_NAME,
        L"DeskWrap XR Compositor - Live Desktop Mirror",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        wr.right - wr.left,
        wr.bottom - wr.top,
        nullptr,
        nullptr,
        GetModuleHandleW(nullptr),
        nullptr
    );

    if (!hwnd) {
        std::cerr << "[Compositor] Failed to create preview window.\n";
        return 1;
    }

    deskwrap::compositor::FlatRenderer renderer;
    g_renderer = &renderer;

    if (!renderer.initialize(hwnd, initial_w, initial_h)) {
        std::cerr << "[Compositor] Failed to initialize Direct3D 11 renderer.\n";
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

    std::cout << "[Compositor] Window created and renderer active. Connecting to daemon...\n";

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
