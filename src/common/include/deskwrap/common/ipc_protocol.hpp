#pragma once

#include <cstdint>
#include <windows.h>

namespace deskwrap::ipc {

constexpr const wchar_t* DEFAULT_PIPE_NAME = L"\\\\.\\pipe\\deskwrap_ipc";
constexpr uint32_t IPC_PROTOCOL_VERSION = 0x00010001;

enum class MessageType : uint32_t {
    Unknown = 0,
    Hello = 1,
    HelloAck = 2,
    SharedTextureHandle = 3,
    FrameAcquired = 4,
    Recenter = 5,
    Heartbeat = 6,
    Teardown = 7
};

#pragma pack(push, 1)

struct MessageHeader {
    uint32_t version = IPC_PROTOCOL_VERSION;
    MessageType type = MessageType::Unknown;
    uint32_t payload_size = 0;
    uint32_t sequence = 0;
};

struct HelloPayload {
    uint32_t client_pid = 0;
};

struct HelloAckPayload {
    uint32_t server_pid = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t refresh_rate_hz = 0;
};

struct SharedTexturePayload {
    uint64_t shared_handle = 0;       // Windows HANDLE as uint64_t for 64-bit IPC safety
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t dxgi_format = 87;        // DXGI_FORMAT_B8G8R8A8_UNORM
    uint64_t creation_time_ns = 0;
    wchar_t shared_name[128] = {0};   // NT shared resource name for OpenSharedResourceByName
    uint32_t has_keyed_mutex = 1;     // 1 if IDXGIKeyedMutex enabled for GPU synchronization
};

struct FrameAcquiredPayload {
    uint64_t frame_index = 0;
    uint64_t timestamp_us = 0;
    RECT dirty_rect{};
};

struct RecenterPayload {
    float yaw_offset_deg = 0.0f;
};

#pragma pack(pop)

} // namespace deskwrap::ipc
