#include <iostream>
#include <string>
#include <filesystem>
#include <atomic>
#include <thread>
#include <chrono>
#include "deskwrap/common/config.hpp"
#include "deskwrap/common/ipc_channel.hpp"
#include "dxgi_capture.hpp"

#ifdef _WIN32
#include <windows.h>
#endif

namespace {

std::atomic<bool> g_running{true};

#ifdef _WIN32
BOOL WINAPI console_ctrl_handler(DWORD ctrl_type) {
    switch (ctrl_type) {
    case CTRL_C_EVENT:
    case CTRL_BREAK_EVENT:
    case CTRL_CLOSE_EVENT:
    case CTRL_SHUTDOWN_EVENT:
        std::cout << "\n[Daemon] Termination requested. Shutting down...\n";
        g_running = false;
        return TRUE;
    default:
        return FALSE;
    }
}
#endif

} // namespace

int main(int argc, char* argv[]) {
    std::filesystem::path config_path;
    bool validate_only = false;
    std::wstring pipe_name = deskwrap::ipc::DEFAULT_PIPE_NAME;
    std::wstring target_device;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--config" && i + 1 < argc) {
            config_path = argv[++i];
        } else if (arg == "--validate-config") {
            validate_only = true;
        } else if (arg == "--pipe" && i + 1 < argc) {
            std::string p = argv[++i];
            pipe_name = std::wstring(p.begin(), p.end());
        } else if (arg == "--device" && i + 1 < argc) {
            std::string d = argv[++i];
            target_device = std::wstring(d.begin(), d.end());
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "DeskWrap XR Daemon\n"
                      << "Usage: deskwrap-daemon [options]\n"
                      << "Options:\n"
                      << "  --config <path>      Specify custom path to config.json\n"
                      << "  --validate-config    Validate config loading and exit\n"
                      << "  --pipe <name>        Named pipe name for IPC\n"
                      << "  --device <name>      Target display device name for capture\n"
                      << "  --help, -h           Show this help message\n";
            return 0;
        }
    }

    std::cout << "[Daemon] Initializing DeskWrap XR Daemon...\n";

    deskwrap::ConfigLoadResult config_res = deskwrap::load_config(config_path);
    const auto& config = config_res.config;
    deskwrap::print_config(config, std::cout);

    if (validate_only) {
        std::cout << "[Daemon] Configuration validation completed.\n";
        return (config_res.status == deskwrap::ConfigLoadStatus::MalformedJson) ? 2 : 0;
    }

#ifdef _WIN32
    SetConsoleCtrlHandler(console_ctrl_handler, TRUE);
#endif

    // Initialize DXGI Desktop Duplication capture
    deskwrap::daemon::DxgiDesktopCapture capture;
    deskwrap::DisplayMode fallback_mode{5760, 1080, 60};
    bool capture_ok = capture.initialize(target_device, fallback_mode);
    if (!capture_ok) {
        std::cerr << "[Daemon] Warning: DXGI Desktop Duplication could not be initialized immediately.\n"
                  << "         Capture will retry when a display is detected.\n";
    }

    // Initialize Named Pipe IPC Server
    deskwrap::ipc::IpcServer ipc_server;
    ipc_server.set_on_message_callback([&](const deskwrap::ipc::MessageHeader& header, const std::vector<uint8_t>& payload) {
        if (header.type == deskwrap::ipc::MessageType::Hello) {
            uint32_t client_pid = 0;
            if (payload.size() >= sizeof(deskwrap::ipc::HelloPayload)) {
                deskwrap::ipc::HelloPayload hp;
                std::memcpy(&hp, payload.data(), sizeof(hp));
                client_pid = hp.client_pid;
            }
            std::cout << "[Daemon] Compositor client connected (PID: " << client_pid << ").\n";

            // Send HelloAck
            deskwrap::ipc::HelloAckPayload ack{};
            ack.server_pid = GetCurrentProcessId();
            ack.width = capture.get_width();
            ack.height = capture.get_height();
            ack.refresh_rate_hz = 60;
            ipc_server.send_message(deskwrap::ipc::MessageType::HelloAck, &ack, sizeof(ack));

            // Send SharedTexturePayload
            deskwrap::ipc::SharedTexturePayload tex_payload = capture.get_shared_texture_payload();
            std::cout << "[Daemon] Sharing texture handle to compositor (Width: " << tex_payload.width
                      << ", Height: " << tex_payload.height
                      << ", KeyedMutex: " << (tex_payload.has_keyed_mutex ? "YES" : "NO") << ")...\n";
            ipc_server.send_shared_texture(tex_payload);
        } else if (header.type == deskwrap::ipc::MessageType::Teardown) {
            std::cout << "[Daemon] Received Teardown request from compositor.\n";
        }
    });

    ipc_server.set_on_disconnect_callback([&]() {
        std::cout << "[Daemon] Compositor disconnected. Ready for reconnect.\n";
    });

    if (!ipc_server.start(pipe_name)) {
        std::wcerr << L"[Daemon] Failed to start IPC server on " << pipe_name << L"\n";
        return 1;
    }

    std::wcout << L"[Daemon] IPC server listening on " << pipe_name << L"\n";
    std::cout << "[Daemon] Capture pipeline active. Press Ctrl+C to terminate daemon.\n";

    // Desktop frame pumping worker thread
    std::thread capture_thread([&]() {
        while (g_running.load()) {
            deskwrap::ipc::RectPod dirty_rect{};
            bool got_frame = capture.acquire_frame(16, dirty_rect);
            if (got_frame) {
                if (ipc_server.is_connected()) {
                    uint64_t now_us = std::chrono::duration_cast<std::chrono::microseconds>(
                        std::chrono::steady_clock::now().time_since_epoch()
                    ).count();
                    ipc_server.send_frame_acquired(capture.get_frame_index(), now_us, dirty_rect);
                }
                capture.release_frame();
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
    });

#ifdef _WIN32
    // Process minimal message loop while running
    MSG msg{};
    while (g_running.load()) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
#else
    while (g_running.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
#endif

    std::cout << "[Daemon] Cleaning up resources...\n";
    g_running = false;
    if (capture_thread.joinable()) {
        capture_thread.join();
    }

    ipc_server.stop();
    capture.shutdown();

    std::cout << "[Daemon] Shutdown complete.\n";
    return 0;
}
