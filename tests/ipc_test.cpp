#include <iostream>
#include <chrono>
#include <thread>
#include <atomic>
#include <cstring>
#include <windows.h>
#include <d3d11.h>
#include <d3d11_1.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#include "deskwrap/common/ipc_protocol.hpp"
#include "deskwrap/common/ipc_channel.hpp"
#include "dxgi_capture.hpp"

using Microsoft::WRL::ComPtr;

#define TEST_CHECK(cond) \
    do { \
        if (!(cond)) { \
            std::cerr << "Assertion failed: " #cond " at " __FILE__ ":" << __LINE__ << "\n"; \
            std::exit(1); \
        } \
    } while (0)

namespace {

void test_ipc_protocol_structs() {
    std::cout << "[TEST] Running test_ipc_protocol_structs...\n";

    deskwrap::ipc::MessageHeader header{};
    header.version = deskwrap::ipc::IPC_PROTOCOL_VERSION;
    header.type = deskwrap::ipc::MessageType::Hello;
    header.payload_size = sizeof(deskwrap::ipc::HelloPayload);
    header.sequence = 1;

    TEST_CHECK(header.version == deskwrap::ipc::IPC_PROTOCOL_VERSION);
    TEST_CHECK(header.type == deskwrap::ipc::MessageType::Hello);

    deskwrap::ipc::SharedTexturePayload payload{};
    payload.shared_handle = 0x12345678ULL;
    payload.width = 1920;
    payload.height = 1080;
    payload.dxgi_format = 87;
    payload.has_keyed_mutex = 1;
    wcscpy_s(payload.shared_name, L"Local\\TestSharedTexture");

    TEST_CHECK(payload.width == 1920);
    TEST_CHECK(payload.height == 1080);
    TEST_CHECK(payload.has_keyed_mutex == 1);
    TEST_CHECK(wcscmp(payload.shared_name, L"Local\\TestSharedTexture") == 0);

    std::cout << "[TEST] test_ipc_protocol_structs PASSED.\n\n";
}

void test_ipc_server_client_communication() {
    std::cout << "[TEST] Running test_ipc_server_client_communication...\n";
    const std::wstring test_pipe = L"\\\\.\\pipe\\deskwrap_test_ipc_comm";

    deskwrap::ipc::IpcServer server;
    deskwrap::ipc::IpcClient client;

    std::atomic<bool> server_received_hello{false};
    std::atomic<uint32_t> received_client_pid{0};

    server.set_on_message_callback([&](const deskwrap::ipc::MessageHeader& hdr, const std::vector<uint8_t>& payload) {
        if (hdr.type == deskwrap::ipc::MessageType::Hello && payload.size() >= sizeof(deskwrap::ipc::HelloPayload)) {
            deskwrap::ipc::HelloPayload hp;
            std::memcpy(&hp, payload.data(), sizeof(hp));
            received_client_pid = hp.client_pid;
            server_received_hello = true;
        }
    });

    std::atomic<bool> client_received_texture{false};
    deskwrap::ipc::SharedTexturePayload client_tex_payload{};

    client.set_on_message_callback([&](const deskwrap::ipc::MessageHeader& hdr, const std::vector<uint8_t>& payload) {
        if (hdr.type == deskwrap::ipc::MessageType::SharedTextureHandle && payload.size() >= sizeof(deskwrap::ipc::SharedTexturePayload)) {
            std::memcpy(&client_tex_payload, payload.data(), sizeof(client_tex_payload));
            client_received_texture = true;
        }
    });

    bool server_started = server.start(test_pipe);
    TEST_CHECK(server_started);

    // Connect client
    bool client_connected = client.connect(test_pipe, 3000);
    TEST_CHECK(client_connected);
    TEST_CHECK(client.is_connected());

    // Give server worker brief moment to accept connection
    for (int i = 0; i < 50 && !server.is_connected(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    TEST_CHECK(server.is_connected());

    // Client sends Hello
    uint32_t my_pid = GetCurrentProcessId();
    bool sent_hello = client.send_hello(my_pid);
    TEST_CHECK(sent_hello);

    for (int i = 0; i < 50 && !server_received_hello.load(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    TEST_CHECK(server_received_hello.load());
    TEST_CHECK(received_client_pid.load() == my_pid);

    // Server sends SharedTexture
    deskwrap::ipc::SharedTexturePayload stp{};
    stp.shared_handle = 0xABCDEF;
    stp.width = 3840;
    stp.height = 1080;
    stp.has_keyed_mutex = 1;
    wcscpy_s(stp.shared_name, L"Local\\DeskWrapTestShared");

    bool sent_tex = server.send_shared_texture(stp);
    TEST_CHECK(sent_tex);

    for (int i = 0; i < 50 && !client_received_texture.load(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    TEST_CHECK(client_received_texture.load());
    TEST_CHECK(client_tex_payload.width == 3840);
    TEST_CHECK(client_tex_payload.height == 1080);
    TEST_CHECK(client_tex_payload.has_keyed_mutex == 1);
    TEST_CHECK(wcscmp(client_tex_payload.shared_name, L"Local\\DeskWrapTestShared") == 0);

    client.disconnect();
    server.stop();

    std::cout << "[TEST] test_ipc_server_client_communication PASSED.\n\n";
}

void test_ipc_reconnect_on_server_restart() {
    std::cout << "[TEST] Running test_ipc_reconnect_on_server_restart...\n";
    const std::wstring test_pipe = L"\\\\.\\pipe\\deskwrap_test_ipc_reconnect";

    auto server = std::make_unique<deskwrap::ipc::IpcServer>();
    deskwrap::ipc::IpcClient client;

    bool server_started = server->start(test_pipe);
    TEST_CHECK(server_started);

    bool client_connected = client.connect(test_pipe, 3000);
    TEST_CHECK(client_connected);
    TEST_CHECK(client.is_connected());

    // Give server worker time to accept
    for (int i = 0; i < 50 && !server->is_connected(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    TEST_CHECK(server->is_connected());

    // Simulate daemon restart: Stop server
    std::cout << "[TEST] Simulating daemon termination (stopping server)...\n";
    server->stop();
    server.reset();

    // Client should observe disconnect
    for (int i = 0; i < 50 && client.is_connected(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    TEST_CHECK(!client.is_connected());
    client.disconnect();

    // Now restart the daemon/server on the same pipe
    std::cout << "[TEST] Simulating daemon restart (starting new server instance)...\n";
    server = std::make_unique<deskwrap::ipc::IpcServer>();
    bool restarted = server->start(test_pipe);
    TEST_CHECK(restarted);

    // Compositor client reconnects cleanly
    bool reconnected = client.connect(test_pipe, 3000);
    TEST_CHECK(reconnected);
    TEST_CHECK(client.is_connected());

    for (int i = 0; i < 50 && !server->is_connected(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    TEST_CHECK(server->is_connected());

    // Verify messages flow again after reconnect
    std::atomic<bool> received_after_reconnect{false};
    client.set_on_message_callback([&](const deskwrap::ipc::MessageHeader& hdr, const std::vector<uint8_t>&) {
        if (hdr.type == deskwrap::ipc::MessageType::HelloAck) {
            received_after_reconnect = true;
        }
    });

    deskwrap::ipc::HelloAckPayload ack{};
    ack.server_pid = GetCurrentProcessId();
    server->send_message(deskwrap::ipc::MessageType::HelloAck, &ack, sizeof(ack));

    for (int i = 0; i < 50 && !received_after_reconnect.load(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    TEST_CHECK(received_after_reconnect.load());

    client.disconnect();
    server->stop();

    std::cout << "[TEST] test_ipc_reconnect_on_server_restart PASSED.\n\n";
}

void test_d3d11_shared_texture_keyed_mutex() {
    std::cout << "[TEST] Running test_d3d11_shared_texture_keyed_mutex...\n";

    // Create Device A (Daemon writer)
    ComPtr<ID3D11Device> device_a;
    ComPtr<ID3D11DeviceContext> context_a;
    D3D_FEATURE_LEVEL fl;
    HRESULT hr = D3D11CreateDevice(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        D3D11_CREATE_DEVICE_BGRA_SUPPORT,
        nullptr,
        0,
        D3D11_SDK_VERSION,
        &device_a,
        &fl,
        &context_a
    );

    if (FAILED(hr)) {
        // Fallback to WARP if hardware unavailable
        hr = D3D11CreateDevice(
            nullptr,
            D3D_DRIVER_TYPE_WARP,
            nullptr,
            D3D11_CREATE_DEVICE_BGRA_SUPPORT,
            nullptr,
            0,
            D3D11_SDK_VERSION,
            &device_a,
            &fl,
            &context_a
        );
    }
    TEST_CHECK(SUCCEEDED(hr));

    // Create shared texture on Device A
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = 640;
    desc.Height = 480;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED_NTHANDLE | D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;

    ComPtr<ID3D11Texture2D> tex_a;
    hr = device_a->CreateTexture2D(&desc, nullptr, &tex_a);
    HANDLE shared_handle = nullptr;
    std::wstring shared_name = L"Local\\DeskWrapKeyedMutexTest_" + std::to_wstring(GetCurrentProcessId());

    bool use_nt = false;
    if (SUCCEEDED(hr)) {
        ComPtr<IDXGIResource1> res1;
        if (SUCCEEDED(tex_a.As(&res1))) {
            hr = res1->CreateSharedHandle(
                nullptr,
                DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE,
                shared_name.c_str(),
                &shared_handle
            );
            if (SUCCEEDED(hr)) {
                use_nt = true;
            }
        }
    }

    if (!use_nt) {
        // Legacy shared fallback
        desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;
        hr = device_a->CreateTexture2D(&desc, nullptr, &tex_a);
        TEST_CHECK(SUCCEEDED(hr));
        ComPtr<IDXGIResource> res;
        TEST_CHECK(SUCCEEDED(tex_a.As(&res)));
        TEST_CHECK(SUCCEEDED(res->GetSharedHandle(&shared_handle)));
        shared_name.clear();
    }

    ComPtr<IDXGIKeyedMutex> mutex_a;
    TEST_CHECK(SUCCEEDED(tex_a.As(&mutex_a)));

    // Create Device B (Compositor reader)
    ComPtr<ID3D11Device> device_b;
    ComPtr<ID3D11DeviceContext> context_b;
    hr = D3D11CreateDevice(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        D3D11_CREATE_DEVICE_BGRA_SUPPORT,
        nullptr,
        0,
        D3D11_SDK_VERSION,
        &device_b,
        &fl,
        &context_b
    );
    if (FAILED(hr)) {
        hr = D3D11CreateDevice(
            nullptr,
            D3D_DRIVER_TYPE_WARP,
            nullptr,
            D3D11_CREATE_DEVICE_BGRA_SUPPORT,
            nullptr,
            0,
            D3D11_SDK_VERSION,
            &device_b,
            &fl,
            &context_b
        );
    }
    TEST_CHECK(SUCCEEDED(hr));

    // Open shared texture on Device B
    ComPtr<ID3D11Texture2D> tex_b;
    ComPtr<ID3D11Device1> device_b1;
    device_b.As(&device_b1);

    if (device_b1 && !shared_name.empty()) {
        hr = device_b1->OpenSharedResourceByName(
            shared_name.c_str(),
            DXGI_SHARED_RESOURCE_READ,
            __uuidof(ID3D11Texture2D),
            &tex_b
        );
    }
    if (FAILED(hr) && shared_handle) {
        if (device_b1) {
            hr = device_b1->OpenSharedResource1(shared_handle, __uuidof(ID3D11Texture2D), &tex_b);
        }
        if (FAILED(hr)) {
            hr = device_b->OpenSharedResource(shared_handle, __uuidof(ID3D11Texture2D), &tex_b);
        }
    }
    TEST_CHECK(SUCCEEDED(hr));
    TEST_CHECK(tex_b != nullptr);

    ComPtr<IDXGIKeyedMutex> mutex_b;
    TEST_CHECK(SUCCEEDED(tex_b.As(&mutex_b)));

    // Synchronization test:
    // 1. Device A acquires Key 0
    hr = mutex_a->AcquireSync(0, 1000);
    TEST_CHECK(SUCCEEDED(hr));

    // 2. Device A releases to Key 1
    hr = mutex_a->ReleaseSync(1);
    TEST_CHECK(SUCCEEDED(hr));

    // 3. Device B acquires Key 1
    hr = mutex_b->AcquireSync(1, 1000);
    TEST_CHECK(SUCCEEDED(hr));

    // 4. Device B releases to Key 0
    hr = mutex_b->ReleaseSync(0);
    TEST_CHECK(SUCCEEDED(hr));

    // 5. Device A acquires Key 0 again
    hr = mutex_a->AcquireSync(0, 1000);
    TEST_CHECK(SUCCEEDED(hr));
    mutex_a->ReleaseSync(1);

    if (shared_handle) {
        CloseHandle(shared_handle);
    }

    std::cout << "[TEST] test_d3d11_shared_texture_keyed_mutex PASSED.\n\n";
}

void test_dxgi_capture_and_ipc_pipeline() {
    std::cout << "[TEST] Running test_dxgi_capture_and_ipc_pipeline...\n";

    deskwrap::daemon::DxgiDesktopCapture capture;
    bool init_ok = capture.initialize();
    if (!init_ok) {
        std::cout << "[TEST] No active display output available for duplication in this session (headless/CI environment). Skipping live capture check.\n\n";
        return;
    }

    TEST_CHECK(capture.get_width() > 0);
    TEST_CHECK(capture.get_height() > 0);
    TEST_CHECK(capture.has_keyed_mutex());

    auto payload = capture.get_shared_texture_payload();
    TEST_CHECK(payload.width == capture.get_width());
    TEST_CHECK(payload.height == capture.get_height());
    TEST_CHECK(payload.has_keyed_mutex == 1);
    TEST_CHECK(payload.shared_handle != 0 || payload.shared_name[0] != 0);

    deskwrap::ipc::RectPod dirty_rect{};
    bool got_frame = capture.acquire_frame(100, dirty_rect);
    if (got_frame) {
        TEST_CHECK(capture.get_frame_index() > 0);
        capture.release_frame();
        std::cout << "[TEST] Successfully captured desktop frame ("
                  << capture.get_width() << "x" << capture.get_height() << ")!\n";
    } else {
        std::cout << "[TEST] acquire_frame timed out gracefully (no screen damage during test window).\n";
    }

    capture.shutdown();
    std::cout << "[TEST] test_dxgi_capture_and_ipc_pipeline PASSED.\n\n";
}

} // namespace

int main() {
    std::cout << "=== DeskWrap XR IPC & Shared Texture Unit Tests ===\n\n";
    test_ipc_protocol_structs();
    test_ipc_server_client_communication();
    test_ipc_reconnect_on_server_restart();
    test_d3d11_shared_texture_keyed_mutex();
    test_dxgi_capture_and_ipc_pipeline();
    std::cout << "All IPC and shared texture tests PASSED successfully!\n";
    return 0;
}
