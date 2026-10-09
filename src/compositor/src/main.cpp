#include <windows.h>
#include <iostream>
#include "viture_client.hpp"
#include "cylinder_mesh.hpp"
#include "d3d11_renderer.hpp"
#include "deskwrap/common/ipc_channel.hpp"

namespace {
deskwrap::compositor::VitureClient g_viture;
deskwrap::compositor::D3D11Renderer g_renderer;
deskwrap::ipc::IpcClient g_ipc;
constexpr int HOTKEY_RECENTER_ID = 101;

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    switch (msg) {
    case WM_HOTKEY:
        if (wparam == HOTKEY_RECENTER_ID) {
            g_viture.recenter();
            g_ipc.send_recenter(0.0f);
        }
        return 0;
    case WM_KEYDOWN:
        if (wparam == VK_SPACE) {
            g_viture.recenter();
            g_ipc.send_recenter(0.0f);
        } else if (wparam == 'W' || wparam == 'w') {
            g_renderer.set_wireframe(!g_renderer.is_wireframe());
        } else if (wparam == VK_ESCAPE) {
            PostQuitMessage(0);
        }
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}
} // namespace

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
    const wchar_t CLASS_NAME[] = L"DeskWrapCompositorWindow";

    WNDCLASSW wc{};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);

    if (!RegisterClassW(&wc)) {
        return 1;
    }

    uint32_t width = 1920;
    uint32_t height = 1200;

    HWND hwnd = CreateWindowExW(
        WS_EX_APPWINDOW,
        CLASS_NAME,
        L"DeskWrap XR Compositor",
        WS_POPUP | WS_VISIBLE,
        0, 0, width, height,
        nullptr,
        nullptr,
        hInstance,
        nullptr
    );

    if (!hwnd) {
        return 1;
    }

    // Register global hotkey Ctrl+Alt+Space for recenter
    RegisterHotKey(hwnd, HOTKEY_RECENTER_ID, MOD_CONTROL | MOD_ALT, VK_SPACE);

    if (!g_renderer.initialize(hwnd, width, height)) {
        std::cerr << "[Compositor] Failed to initialize D3D11 renderer.\n";
        return 1;
    }

    // Generate inward cylindrical mesh
    deskwrap::CylinderGeometry geom{};
    geom.radius = 1.8f;
    geom.arc_degrees = 180.0f;
    geom.height = 1.0f;
    deskwrap::compositor::MeshData mesh = deskwrap::compositor::CylinderMeshGenerator::generate(geom);
    g_renderer.set_mesh(mesh);

    // Setup Named Pipe IPC Client to receive shared desktop texture
    g_ipc.set_on_message_callback([](const deskwrap::ipc::MessageHeader& header, const std::vector<uint8_t>& data) {
        if (header.type == deskwrap::ipc::MessageType::SharedTextureHandle && data.size() >= sizeof(deskwrap::ipc::SharedTexturePayload)) {
            deskwrap::ipc::SharedTexturePayload payload;
            std::memcpy(&payload, data.data(), sizeof(payload));
            g_renderer.import_shared_texture(
                reinterpret_cast<HANDLE>(payload.shared_handle),
                payload.shared_name,
                payload.has_keyed_mutex != 0
            );
        }
    });
    g_ipc.connect(deskwrap::ipc::DEFAULT_PIPE_NAME, 2000);
    g_ipc.send_hello(GetCurrentProcessId());

    // Initialize VITURE SDK with Beast PID (or default)
    // VID: 0x35CA. Typical PID range for Gen2 Beast: 0x1011..0x1013
    g_viture.initialize(0x1011);

    // Main render loop
    MSG msg{};
    bool running = true;
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

        // Obtain camera orientation from latest VITURE IMU pose
        deskwrap::math::Quaternion cam_rot = g_viture.get_camera_orientation();

        // 45 degree FOV vertical
        float fov_y = 45.0f * deskwrap::math::DEG_TO_RAD;
        g_renderer.render_frame(cam_rot, fov_y);

        Sleep(1);
    }

    UnregisterHotKey(hwnd, HOTKEY_RECENTER_ID);
    g_ipc.disconnect();
    g_viture.shutdown();
    g_renderer.shutdown();

    return 0;
}
